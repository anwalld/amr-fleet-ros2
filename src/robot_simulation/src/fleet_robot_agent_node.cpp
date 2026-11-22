// 通用车队代理（M2）。两种模式：
//   mode:=follow_fleet_station（默认，兼容原 robot2_fleet_demo_driver_node）：按车队状态里的
//     current_station_id 点到点直开，不申请路权。Gazebo 两车演示场景用它。
//   mode:=task_agent：领取派给自己的车队任务（/v2/list_remote_tasks），用
//     /v2/list_station_routes 算路径，逐段 /v2/request_route_segment → 行驶 →
//     /v2/release_route_segment，任务进度回报 /v2/update_remote_task_state。
//     kinematic_sim:=true 时不需要 Gazebo：按自己算出的速度指令积分位置，发布 <ns>/odom。
//     traffic_reservation_mode:=whole_route 时改为整条路径一次申请、拿不到隔几秒重试（对照组）。
//     M3：申请时带本程目的地 goal_station；按授予应答给出的资源号释放（不自己拼资源号）；
//     执行 /traffic/yield 的让路指令（reroute 改道 / backoff 退到避让点 / pause 暂停）。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces_fleet/srv/list_fleet_robots.hpp"
#include "robot_interfaces_fleet/srv/list_remote_tasks.hpp"
#include "robot_simulation/agent_idle.hpp"
#include "robot_simulation/agent_yield.hpp"
#include "robot_interfaces_fleet/srv/update_remote_task_state.hpp"
#include "robot_interfaces_mission/msg/traffic_grant.hpp"
#include "robot_interfaces_mission/msg/traffic_state.hpp"
#include "robot_interfaces_mission/msg/traffic_yield.hpp"
#include "robot_interfaces_mission/srv/list_station_routes.hpp"
#include "robot_interfaces_mission/srv/release_route_segment.hpp"
#include "robot_interfaces_mission/srv/request_route_segment.hpp"
#include "std_msgs/msg/string.hpp"
#include "robot_interfaces_mission/srv/list_stations.hpp"
#include "robot_interfaces_fleet/srv/update_fleet_robot_state.hpp"

using namespace std::chrono_literals;

namespace {

struct Pose2D {
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

double YawFromQuaternion(const geometry_msgs::msg::Quaternion& q) {
  const double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
  const double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
  return std::atan2(siny_cosp, cosy_cosp);
}

double NormalizeAngle(double angle) {
  constexpr double kPi = 3.14159265358979323846;
  while (angle > kPi) {
    angle -= 2.0 * kPi;
  }
  while (angle < -kPi) {
    angle += 2.0 * kPi;
  }
  return angle;
}

}  // namespace

// 旧模式（mode:=follow_fleet_station）：原 robot2_fleet_demo_driver_node，按车队状态里的
// current_station_id 点到点直开，不申请路权。Gazebo 两车演示场景继续用它。
class FleetStationFollower final : public rclcpp::Node {
 public:
  explicit FleetStationFollower(const rclcpp::NodeOptions& options)
      : Node("fleet_robot_agent_node", options) {
    robot_id_ = declare_parameter<std::string>("robot_id", "robot_2");
    initial_station_id_ = declare_parameter<std::string>("initial_station_id", "dock");
    stations_service_ = declare_parameter<std::string>("stations_service", "/v2/list_stations");
    fleet_service_ = declare_parameter<std::string>("fleet_service", "/v2/list_fleet_robots");
    update_fleet_service_ =
        declare_parameter<std::string>("update_fleet_service", "/v2/update_fleet_robot_state");
    odom_topic_ = declare_parameter<std::string>("odom_topic", "/robot_2/odom");
    main_odom_topic_ = declare_parameter<std::string>("main_odom_topic", "/odom");
    cmd_vel_topic_ = declare_parameter<std::string>("cmd_vel_topic", "/robot_2/cmd_vel");
    max_linear_mps_ = declare_parameter<double>("max_linear_mps", 0.72);
    max_angular_radps_ = declare_parameter<double>("max_angular_radps", 1.25);
    goal_tolerance_m_ = declare_parameter<double>("goal_tolerance_m", 0.22);
    slow_radius_m_ = declare_parameter<double>("slow_radius_m", 1.0);
    min_separation_m_ = declare_parameter<double>("min_separation_m", 1.25);
    control_period_s_ = declare_parameter<double>("control_period_s", 0.1);
    linear_heading_tolerance_rad_ = declare_parameter<double>("linear_heading_tolerance_rad", 0.18);
    linear_command_sign_ = declare_parameter<double>("linear_command_sign", 1.0);
    odom_yaw_sign_ = declare_parameter<double>("odom_yaw_sign", -1.0);
    angular_command_sign_ = declare_parameter<double>("angular_command_sign", -1.0);

    stations_client_ = create_client<robot_interfaces_mission::srv::ListStations>(stations_service_);
    fleet_client_ = create_client<robot_interfaces_fleet::srv::ListFleetRobots>(fleet_service_);
    update_fleet_client_ =
        create_client<robot_interfaces_fleet::srv::UpdateFleetRobotState>(update_fleet_service_);
    cmd_pub_ = create_publisher<geometry_msgs::msg::Twist>(cmd_vel_topic_, 10);
    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
        odom_topic_, 10, [this](nav_msgs::msg::Odometry::SharedPtr msg) {
          pose_.x = msg->pose.pose.position.x;
          pose_.y = msg->pose.pose.position.y;
          pose_.yaw = YawFromQuaternion(msg->pose.pose.orientation);
          have_pose_ = true;
        });
    main_odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
        main_odom_topic_, 10, [this](nav_msgs::msg::Odometry::SharedPtr msg) {
          main_pose_.x = msg->pose.pose.position.x;
          main_pose_.y = msg->pose.pose.position.y;
          main_pose_.yaw = YawFromQuaternion(msg->pose.pose.orientation);
          have_main_pose_ = true;
        });

    timer_ = create_wall_timer(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(control_period_s_)),
        [this]() { Tick(); });
  }

 private:
  void Tick() {
    PollStations();
    PollFleet();
    UpdateWorldOffset();

    if (!have_pose_ || target_station_id_.empty() ||
        stations_.find(target_station_id_) == stations_.end() || !have_world_offset_) {
      PublishStop();
      return;
    }

    const auto target = stations_.at(target_station_id_);
    const auto world_pose = CurrentWorldPose();
    const double dx = target.x - world_pose.x;
    const double dy = target.y - world_pose.y;
    const double distance = std::hypot(dx, dy);
    if (distance <= goal_tolerance_m_) {
      PublishStop();
      ReportArrival();
      return;
    }

    if (TooCloseToMainRobot()) {
      PublishStop();
      return;
    }

    const double target_heading = std::atan2(dy, dx);
    const double heading_error = NormalizeAngle(target_heading - world_pose.yaw);
    geometry_msgs::msg::Twist cmd;
    cmd.angular.z =
        angular_command_sign_ *
        std::clamp(1.6 * heading_error, -max_angular_radps_, max_angular_radps_);
    if (std::abs(heading_error) < linear_heading_tolerance_rad_) {
      const double distance_scale = std::clamp(distance / slow_radius_m_, 0.25, 1.0);
      cmd.linear.x = linear_command_sign_ * max_linear_mps_ * distance_scale;
    }
    cmd_pub_->publish(cmd);
  }

  void PollStations() {
    if (stations_request_pending_ || !ShouldPoll(last_station_poll_, 2s) ||
        !stations_client_->service_is_ready()) {
      return;
    }
    stations_request_pending_ = true;
    auto request = std::make_shared<robot_interfaces_mission::srv::ListStations::Request>();
    stations_client_->async_send_request(
        request, [this](rclcpp::Client<robot_interfaces_mission::srv::ListStations>::SharedFuture future) {
          stations_request_pending_ = false;
          const auto response = future.get();
          if (!response->success) {
            return;
          }
          std::map<std::string, Pose2D> stations;
          for (std::size_t i = 0; i < response->station_ids.size(); ++i) {
            if (i >= response->x.size() || i >= response->y.size() || i >= response->yaw.size()) {
              continue;
            }
            stations[response->station_ids[i]] = {response->x[i], response->y[i], response->yaw[i]};
          }
          if (!stations.empty()) {
            stations_ = std::move(stations);
          }
        });
  }

  void PollFleet() {
    if (fleet_request_pending_ || !ShouldPoll(last_fleet_poll_, 750ms) ||
        !fleet_client_->service_is_ready()) {
      return;
    }
    fleet_request_pending_ = true;
    auto request = std::make_shared<robot_interfaces_fleet::srv::ListFleetRobots::Request>();
    request->include_disabled = true;
    fleet_client_->async_send_request(
        request,
        [this](rclcpp::Client<robot_interfaces_fleet::srv::ListFleetRobots>::SharedFuture future) {
          fleet_request_pending_ = false;
          const auto response = future.get();
          if (!response->success) {
            return;
          }
          for (std::size_t i = 0; i < response->robot_ids.size(); ++i) {
            if (response->robot_ids[i] != robot_id_) {
              continue;
            }
            if (i < response->current_station_ids.size()) {
              SetTargetStation(response->current_station_ids[i]);
            }
            return;
          }
        });
  }

  void SetTargetStation(const std::string& station_id) {
    if (station_id.empty() || station_id == target_station_id_) {
      return;
    }
    target_station_id_ = station_id;
    arrival_reported_ = false;
  }

  bool ShouldPoll(
      std::chrono::steady_clock::time_point& last_poll,
      const std::chrono::steady_clock::duration period) {
    const auto now = std::chrono::steady_clock::now();
    if (last_poll.time_since_epoch().count() != 0 && now - last_poll < period) {
      return false;
    }
    last_poll = now;
    return true;
  }

  bool TooCloseToMainRobot() const {
    if (!have_main_pose_ || min_separation_m_ <= 0.0) {
      return false;
    }
    const auto world_pose = CurrentWorldPose();
    return std::hypot(world_pose.x - main_pose_.x, world_pose.y - main_pose_.y) < min_separation_m_;
  }

  void UpdateWorldOffset() {
    if (have_world_offset_ || !have_pose_ || stations_.empty()) {
      return;
    }
    auto station = stations_.find(initial_station_id_);
    if (station == stations_.end() && !target_station_id_.empty()) {
      station = stations_.find(target_station_id_);
    }
    if (station == stations_.end()) {
      return;
    }
    world_offset_x_ = station->second.x - pose_.x;
    world_offset_y_ = station->second.y - pose_.y;
    have_world_offset_ = true;
  }

  Pose2D CurrentWorldPose() const {
    return {pose_.x + world_offset_x_, pose_.y + world_offset_y_, odom_yaw_sign_ * pose_.yaw};
  }

  void ReportArrival() {
    if (arrival_reported_ || arrival_report_pending_ || !update_fleet_client_->service_is_ready()) {
      return;
    }
    arrival_report_pending_ = true;
    auto request = std::make_shared<robot_interfaces_fleet::srv::UpdateFleetRobotState::Request>();
    request->robot_id = robot_id_;
    request->state = "available";
    request->current_station_id = target_station_id_;
    request->update_state = true;
    request->update_current_station_id = true;
    update_fleet_client_->async_send_request(
        request,
        [this](rclcpp::Client<robot_interfaces_fleet::srv::UpdateFleetRobotState>::SharedFuture future) {
          arrival_report_pending_ = false;
          arrival_reported_ = future.get()->success;
        });
  }

  void PublishStop() {
    geometry_msgs::msg::Twist cmd;
    cmd_pub_->publish(cmd);
  }

  std::string robot_id_;
  std::string initial_station_id_;
  std::string stations_service_;
  std::string fleet_service_;
  std::string update_fleet_service_;
  std::string odom_topic_;
  std::string main_odom_topic_;
  std::string cmd_vel_topic_;
  double max_linear_mps_ = 0.72;
  double max_angular_radps_ = 1.25;
  double goal_tolerance_m_ = 0.22;
  double slow_radius_m_ = 1.0;
  double min_separation_m_ = 1.25;
  double control_period_s_ = 0.1;
  double linear_heading_tolerance_rad_ = 0.18;
  double linear_command_sign_ = 1.0;
  double odom_yaw_sign_ = -1.0;
  double angular_command_sign_ = -1.0;
  bool have_pose_ = false;
  bool have_main_pose_ = false;
  bool have_world_offset_ = false;
  bool stations_request_pending_ = false;
  bool fleet_request_pending_ = false;
  bool arrival_report_pending_ = false;
  bool arrival_reported_ = false;
  std::string target_station_id_;
  Pose2D pose_;
  Pose2D main_pose_;
  double world_offset_x_ = 0.0;
  double world_offset_y_ = 0.0;
  std::map<std::string, Pose2D> stations_;
  std::chrono::steady_clock::time_point last_station_poll_;
  std::chrono::steady_clock::time_point last_fleet_poll_;

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr main_odom_sub_;
  rclcpp::Client<robot_interfaces_mission::srv::ListStations>::SharedPtr stations_client_;
  rclcpp::Client<robot_interfaces_fleet::srv::ListFleetRobots>::SharedPtr fleet_client_;
  rclcpp::Client<robot_interfaces_fleet::srv::UpdateFleetRobotState>::SharedPtr update_fleet_client_;
};


namespace {

struct DirectedEdge {
  std::string to;
  double distance_m = 0.0;
};

// 资源号只由路权服务生成（规格里写明了格式）。代理只解析，不拼接：
//   route_edge:<a>__<b> → 无序站点对；route_node:<站点> → 站点。
using EdgeKey = std::pair<std::string, std::string>;

EdgeKey MakeEdgeKey(const std::string& a, const std::string& b) {
  return a <= b ? EdgeKey{a, b} : EdgeKey{b, a};
}

std::optional<EdgeKey> ParseEdgeResource(const std::string& resource_id) {
  const std::string prefix = "route_edge:";
  if (resource_id.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }
  const auto separator = resource_id.find("__", prefix.size());
  if (separator == std::string::npos) {
    return std::nullopt;
  }
  return MakeEdgeKey(
      resource_id.substr(prefix.size(), separator - prefix.size()),
      resource_id.substr(separator + 2));
}

std::optional<std::string> ParseNodeResource(const std::string& resource_id) {
  const std::string prefix = "route_node:";
  if (resource_id.rfind(prefix, 0) != 0 || resource_id.size() == prefix.size()) {
    return std::nullopt;
  }
  return resource_id.substr(prefix.size());
}

geometry_msgs::msg::Quaternion QuaternionFromYaw(const double yaw) {
  geometry_msgs::msg::Quaternion q;
  q.z = std::sin(yaw * 0.5);
  q.w = std::cos(yaw * 0.5);
  return q;
}

}  // namespace

// 任务代理（mode:=task_agent）。单线程执行器，所有服务调用都是异步的，
// 由 20 Hz 控制定时器推进状态机。
class FleetTaskAgent final : public rclcpp::Node {
 public:
  explicit FleetTaskAgent(const rclcpp::NodeOptions& options)
      : Node("fleet_robot_agent_node", options) {
    robot_id_ = declare_parameter<std::string>("robot_id", "robot_2");
    topic_namespace_ = declare_parameter<std::string>("topic_namespace", "/" + robot_id_);
    initial_station_id_ = declare_parameter<std::string>("initial_station_id", "dock");
    home_station_id_ = declare_parameter<std::string>("home_station_id", initial_station_id_);
    kinematic_sim_ = declare_parameter<bool>("kinematic_sim", true);
    traffic_mode_ = declare_parameter<std::string>("traffic_reservation_mode", "rolling");
    max_linear_mps_ = declare_parameter<double>("max_linear_mps", 0.8);
    max_angular_radps_ = declare_parameter<double>("max_angular_radps", 1.8);
    goal_tolerance_m_ = declare_parameter<double>("goal_tolerance_m", 0.08);
    dwell_s_ = declare_parameter<double>("dwell_s", 3.0);
    whole_route_retry_s_ = declare_parameter<double>("whole_route_retry_s", 2.0);
    rolling_rerequest_s_ = declare_parameter<double>("rolling_rerequest_s", 2.0);
    return_home_when_idle_ = declare_parameter<bool>("return_home_when_idle", true);
    control_period_s_ = declare_parameter<double>("control_period_s", 0.05);

    stations_client_ = create_client<robot_interfaces_mission::srv::ListStations>("/v2/list_stations");
    routes_client_ =
        create_client<robot_interfaces_mission::srv::ListStationRoutes>("/v2/list_station_routes");
    tasks_client_ = create_client<robot_interfaces_fleet::srv::ListRemoteTasks>("/v2/list_remote_tasks");
    task_state_client_ =
        create_client<robot_interfaces_fleet::srv::UpdateRemoteTaskState>("/v2/update_remote_task_state");
    fleet_state_client_ =
        create_client<robot_interfaces_fleet::srv::UpdateFleetRobotState>("/v2/update_fleet_robot_state");
    request_client_ =
        create_client<robot_interfaces_mission::srv::RequestRouteSegment>("/v2/request_route_segment");
    release_client_ =
        create_client<robot_interfaces_mission::srv::ReleaseRouteSegment>("/v2/release_route_segment");

    odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(topic_namespace_ + "/odom", 10);
    cmd_pub_ = create_publisher<geometry_msgs::msg::Twist>(topic_namespace_ + "/cmd_vel", 10);
    events_pub_ = create_publisher<std_msgs::msg::String>("/fleet/agent_events", rclcpp::QoS(100).reliable());
    if (!kinematic_sim_) {
      odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
          topic_namespace_ + "/odom", 10, [this](nav_msgs::msg::Odometry::SharedPtr msg) {
            pose_.x = msg->pose.pose.position.x;
            pose_.y = msg->pose.pose.position.y;
            pose_.yaw = YawFromQuaternion(msg->pose.pose.orientation);
            have_pose_ = true;
          });
    }
    grant_sub_ = create_subscription<robot_interfaces_mission::msg::TrafficGrant>(
        "/traffic/grants", rclcpp::QoS(50).reliable(),
        [this](robot_interfaces_mission::msg::TrafficGrant::SharedPtr msg) {
          if (msg->robot_id != robot_id_) {
            return;
          }
          if (msg->event == "replan") {
            replan_requested_ = true;
          }
          grant_hint_ = true;  // 立刻重申请一次（已授予的会直接返回 granted）
        });
    traffic_state_sub_ = create_subscription<robot_interfaces_mission::msg::TrafficState>(
        "/traffic/state", 10, [this](robot_interfaces_mission::msg::TrafficState::SharedPtr msg) {
          std::set<EdgeKey> blocked;
          bool own_any = false;
          for (std::size_t i = 0; i < msg->resource_ids.size() && i < msg->owner_ids.size(); ++i) {
            if (msg->owner_ids[i].rfind("traffic_block:", 0) == 0) {
              if (const auto edge = ParseEdgeResource(msg->resource_ids[i])) {
                blocked.insert(*edge);
              }
            }
            own_any = own_any || msg->owner_ids[i] == robot_id_;
          }
          blocked_edges_ = std::move(blocked);
          if (paused_ && !own_any) {
            // 兜底暂停后运维清除了本车的路权：恢复，先重新占住所在路口。
            paused_ = false;
            held_.clear();
            phase_ = Phase::kReclaim;
            RCLCPP_WARN(get_logger(), "%s: resumed after deadlock pause", robot_id_.c_str());
          }
        });
    yield_sub_ = create_subscription<robot_interfaces_mission::msg::TrafficYield>(
        "/traffic/yield", rclcpp::QoS(50).reliable(),
        [this](robot_interfaces_mission::msg::TrafficYield::SharedPtr msg) {
          if (msg->robot_id != robot_id_) {
            return;
          }
          RCLCPP_WARN(
              get_logger(), "%s: yield %s (%zu stations, hold %.1fs)", robot_id_.c_str(),
              msg->action.c_str(), msg->station_path.size(), msg->hold_s);
          if (msg->action == "pause") {
            paused_ = true;
            pending_yield_.reset();
            return;
          }
          pending_yield_ = *msg;
        });

    timer_ = create_wall_timer(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(control_period_s_)),
        [this]() { Tick(); });
    RCLCPP_INFO(
        get_logger(), "fleet task agent %s start=%s home=%s kinematic=%s traffic=%s",
        robot_id_.c_str(), initial_station_id_.c_str(), home_station_id_.c_str(),
        kinematic_sim_ ? "true" : "false", traffic_mode_.c_str());
  }

 private:
  enum class Phase {
    kLoadMap, kClaimStart, kIdle, kRoute, kWaitGrant, kDrive, kDwell, kYieldHold, kReclaim
  };
  enum class Goal { kNone, kPickup, kDropoff, kHome };

  struct StationPose {
    double x = 0.0;
    double y = 0.0;
    double yaw = 0.0;
  };

  double Now() const { return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count(); }
  bool WholeRoute() const { return traffic_mode_ == "whole_route"; }

  // ---------------------------------------------------------------- main loop
  void Tick() {
    const double t = Now();
    const double dt = last_tick_ > 0.0 ? std::min(t - last_tick_, 0.2) : 0.0;
    last_tick_ = t;

    geometry_msgs::msg::Twist cmd;
    if (phase_ == Phase::kDrive) {
      cmd = DriveCommand();
    }
    if (kinematic_sim_ && have_pose_) {
      Integrate(cmd, dt);
    } else if (!kinematic_sim_) {
      cmd_pub_->publish(cmd);
    }
    if (have_pose_ && t - last_odom_pub_ >= 0.1) {
      PublishOdom();
      last_odom_pub_ = t;
    }

    switch (phase_) {
      case Phase::kLoadMap: LoadMap(); break;
      case Phase::kClaimStart: ClaimStart(); break;
      case Phase::kIdle: Idle(); break;
      case Phase::kRoute: Route(); break;
      case Phase::kWaitGrant: WaitGrant(); break;
      case Phase::kDrive: CheckArrival(); break;
      case Phase::kDwell: Dwell(); break;
      case Phase::kYieldHold: YieldHold(); break;
      case Phase::kReclaim: Reclaim(); break;
    }
  }

  // ---------------------------------------------------------------- map
  void LoadMap() {
    if (!stations_client_->service_is_ready() || !routes_client_->service_is_ready()) {
      return;
    }
    if (!stations_pending_ && stations_.empty() && t_ok(last_map_request_, 1.0)) {
      stations_pending_ = true;
      stations_client_->async_send_request(
          std::make_shared<robot_interfaces_mission::srv::ListStations::Request>(),
          [this](rclcpp::Client<robot_interfaces_mission::srv::ListStations>::SharedFuture f) {
            stations_pending_ = false;
            const auto r = f.get();
            for (std::size_t i = 0; i < r->station_ids.size() && i < r->x.size(); ++i) {
              stations_[r->station_ids[i]] = {r->x[i], r->y[i], i < r->yaw.size() ? r->yaw[i] : 0.0};
            }
          });
    }
    if (!routes_pending_ && graph_.empty() && t_ok(last_route_request_, 1.0)) {
      routes_pending_ = true;
      routes_client_->async_send_request(
          std::make_shared<robot_interfaces_mission::srv::ListStationRoutes::Request>(),
          [this](rclcpp::Client<robot_interfaces_mission::srv::ListStationRoutes>::SharedFuture f) {
            routes_pending_ = false;
            const auto r = f.get();
            std::map<std::string, std::vector<DirectedEdge>> graph;
            for (std::size_t i = 0; i < r->from_station.size(); ++i) {
              if (i < r->enabled.size() && !r->enabled[i]) {
                continue;
              }
              const double d = i < r->distance_m.size() ? r->distance_m[i] : 1.0;
              graph[r->from_station[i]].push_back({r->to_station[i], d});
              if (i < r->bidirectional.size() && r->bidirectional[i]) {
                graph[r->to_station[i]].push_back({r->from_station[i], d});
              }
            }
            graph_ = std::move(graph);
          });
    }
    if (!stations_.empty() && !graph_.empty()) {
      const auto it = stations_.find(initial_station_id_);
      if (it == stations_.end()) {
        RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "unknown initial station %s",
                              initial_station_id_.c_str());
        return;
      }
      if (kinematic_sim_) {
        pose_ = {it->second.x, it->second.y, it->second.yaw};
        have_pose_ = true;
      }
      current_station_ = initial_station_id_;
      phase_ = Phase::kClaimStart;
    }
  }

  bool t_ok(double& last, const double period) {
    const double t = Now();
    if (t - last < period) {
      return false;
    }
    last = t;
    return true;
  }

  // 起点：占住所在站点的路口，再在车队里登记为 available。
  void ClaimStart() {
    if (request_pending_ || !request_client_->service_is_ready() ||
        !fleet_state_client_->service_is_ready() || !t_ok(last_claim_, 1.0)) {
      return;
    }
    SendRouteRequest("", current_station_, {}, [this](const auto& r) {
      if (!r->granted) {
        return;  // 下一秒重试（rolling 下已在队列里）
      }
      held_.insert(r->resource_ids.begin(), r->resource_ids.end());
      UpdateFleetState("available", current_station_);
      phase_ = Phase::kIdle;
      RCLCPP_INFO(get_logger(), "%s ready at %s", robot_id_.c_str(), current_station_.c_str());
    });
  }

  // ---------------------------------------------------------------- tasks
  void Idle() { PollTasks(); }

  void PollTasks() {
    if (tasks_pending_ || !tasks_client_->service_is_ready() || !t_ok(last_task_poll_, 0.5)) {
      return;
    }
    tasks_pending_ = true;
    auto req = std::make_shared<robot_interfaces_fleet::srv::ListRemoteTasks::Request>();
    req->robot_id = robot_id_;
    req->state = "DISPATCHED_REMOTE";
    tasks_client_->async_send_request(
        req, [this](rclcpp::Client<robot_interfaces_fleet::srv::ListRemoteTasks>::SharedFuture f) {
          tasks_pending_ = false;
          const auto r = f.get();
          if (!r->success) {
            return;
          }
          const bool heading_home =
              goal_ == Goal::kHome &&
              (phase_ == Phase::kRoute || (phase_ == Phase::kWaitGrant && !request_pending_));
          if (!r->task_ids.empty() && task_id_.empty() && (phase_ == Phase::kIdle || heading_home)) {
            StartTask(r->task_ids[0], r->pickup_stations[0], r->dropoff_stations[0],
                      r->priorities.empty() ? 0 : r->priorities[0]);
            return;
          }
          if (r->task_ids.empty() &&
              robot_simulation::ShouldReturnHomeWhenIdle(
                  return_home_when_idle_, !task_id_.empty(), phase_ == Phase::kIdle,
                  completed_any_task_, current_station_, home_station_id_)) {
            goal_ = Goal::kHome;
            goal_station_ = home_station_id_;
            priority_ = 0;
            phase_ = Phase::kRoute;
          }
        });
  }

  // 兜底暂停解除后：重新占住所在路口，再继续本程。
  void Reclaim() {
    if (request_pending_ || !t_ok(last_claim_, 1.0)) {
      return;
    }
    SendRouteRequest("", current_station_, {}, [this](const auto& r) {
      if (!r->granted || phase_ != Phase::kReclaim) {
        return;
      }
      held_.insert(r->resource_ids.begin(), r->resource_ids.end());
      phase_ = goal_ == Goal::kNone ? Phase::kIdle : Phase::kRoute;
    });
  }

  void StartTask(const std::string& task_id, const std::string& pickup,
                 const std::string& dropoff, const int priority) {
    ClearYieldState();
    task_id_ = task_id;
    pickup_ = pickup;
    dropoff_ = dropoff;
    priority_ = priority;
    task_started_t_ = Now();
    task_wait_s_ = 0.0;
    task_distance_m_ = 0.0;
    UpdateTaskState("RUNNING", "accepted by " + robot_id_ + " at " + current_station_);
    UpdateFleetState("busy", current_station_);
    goal_ = Goal::kPickup;
    goal_station_ = pickup_;
    phase_ = Phase::kRoute;
  }

  // ---------------------------------------------------------------- routing
  std::vector<std::string> ShortestPath(const std::string& from, const std::string& to) const {
    if (from == to) {
      return {from};
    }
    std::map<std::string, double> dist;
    std::map<std::string, std::string> prev;
    using Item = std::pair<double, std::string>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    dist[from] = 0.0;
    open.push({0.0, from});
    while (!open.empty()) {
      const auto [d, u] = open.top();
      open.pop();
      if (d > dist[u]) {
        continue;
      }
      if (u == to) {
        break;
      }
      const auto it = graph_.find(u);
      if (it == graph_.end()) {
        continue;
      }
      for (const auto& e : it->second) {
        const auto key = MakeEdgeKey(u, e.to);
        if (blocked_edges_.count(key) != 0U || avoid_edges_.count(key) != 0U ||
            (e.to != to && avoid_nodes_.count(e.to) != 0U)) {
          continue;
        }
        const double nd = d + e.distance_m;
        const auto old = dist.find(e.to);
        if (old == dist.end() || nd < old->second) {
          dist[e.to] = nd;
          prev[e.to] = u;
          open.push({nd, e.to});
        }
      }
    }
    if (dist.find(to) == dist.end()) {
      return {};
    }
    std::vector<std::string> path{to};
    while (path.back() != from) {
      path.push_back(prev.at(path.back()));
    }
    std::reverse(path.begin(), path.end());
    return path;
  }

  void Route() {
    if (task_id_.empty() && goal_ == Goal::kHome) {
      PollTasks();  // 回停车位途中有新任务就改去取货
      if (!task_id_.empty()) {
        return;
      }
    }
    if (current_station_ == RouteTarget()) {
      ArrivedAtTarget();
      return;
    }
    path_ = ShortestPath(current_station_, RouteTarget());
    if (path_.size() < 2U) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "%s: no path %s -> %s",
                           robot_id_.c_str(), current_station_.c_str(), RouteTarget().c_str());
      return;
    }
    path_index_ = 1;
    wait_started_t_ = Now();
    phase_ = Phase::kWaitGrant;
    last_request_t_ = 0.0;
    WaitGrant();
  }

  void WaitGrant() {
    if (task_id_.empty() && goal_ == Goal::kHome) {
      PollTasks();  // 在站点等路权时也可以改去接新任务
    }
    if (request_pending_ || phase_ != Phase::kWaitGrant || paused_) {
      return;
    }
    if (pending_yield_.has_value()) {
      ApplyYield(*pending_yield_);
      pending_yield_.reset();
      return;
    }
    const double t = Now();
    const double period = WholeRoute() ? whole_route_retry_s_ + retry_jitter_ : rolling_rerequest_s_;
    if (!grant_hint_ && last_request_t_ > 0.0 && t - last_request_t_ < period) {
      return;
    }
    if (replan_requested_) {
      replan_requested_ = false;
      phase_ = Phase::kRoute;
      return;
    }
    grant_hint_ = false;
    last_request_t_ = t;
    const std::string next = robot_simulation::NextStation(path_, path_index_);
    const std::vector<std::string> whole =
        WholeRoute() ? std::vector<std::string>(path_.begin() + path_index_ - 1, path_.end())
                     : std::vector<std::string>{};
    SendRouteRequest(current_station_, next, whole, [this, next](const auto& r) {
      if (phase_ != Phase::kWaitGrant) {
        // 等待期间改了目标（接到新任务）：这份授予用不上，还回去，只留当前路口。
        if (r->granted) {
          held_.insert(r->resource_ids.begin(), r->resource_ids.end());
          std::vector<std::string> unused;
          for (const auto& id : r->resource_ids) {
            if (ParseNodeResource(id) != std::optional<std::string>(current_station_)) {
              unused.push_back(id);
            }
          }
          Release(unused);
        }
        return;
      }
      if (r->granted) {
        held_.insert(r->resource_ids.begin(), r->resource_ids.end());
        task_wait_s_ += Now() - wait_started_t_;
        if (!task_id_.empty()) {
          total_wait_s_ += Now() - wait_started_t_;
        }
        target_station_ = next;
        phase_ = Phase::kDrive;
        return;
      }
      if (r->replan_required) {
        for (const auto& id : r->blocking_resource_ids) {
          if (const auto edge = ParseEdgeResource(id)) {
            blocked_edges_.insert(*edge);
          }
        }
        phase_ = Phase::kRoute;
        return;
      }
      if (WholeRoute()) {
        std::uniform_real_distribution<double> jitter(0.0, 1.0);
        retry_jitter_ = jitter(rng_);
      }
      // rolling：已在队列里，等 /traffic/grants 或定时重申请。
    });
  }

  void SendRouteRequest(
      const std::string& from, const std::string& to, const std::vector<std::string>& path,
      std::function<void(const robot_interfaces_mission::srv::RequestRouteSegment::Response::SharedPtr&)>
          on_done) {
    request_pending_ = true;
    auto req = std::make_shared<robot_interfaces_mission::srv::RequestRouteSegment::Request>();
    req->robot_id = robot_id_;
    req->from_station = from;
    req->to_station = to;
    req->priority = priority_;
    req->station_path = path;
    req->goal_station = goal_station_;
    request_client_->async_send_request(
        req, [this, on_done](
                 rclcpp::Client<robot_interfaces_mission::srv::RequestRouteSegment>::SharedFuture f) {
          request_pending_ = false;
          on_done(f.get());
        });
  }

  void Release(const std::vector<std::string>& ids) {
    if (ids.empty()) {
      return;
    }
    for (const auto& id : ids) {
      held_.erase(id);
    }
    auto req = std::make_shared<robot_interfaces_mission::srv::ReleaseRouteSegment::Request>();
    req->robot_id = robot_id_;
    req->resource_ids = ids;
    release_client_->async_send_request(req);
  }

  // ---------------------------------------------------------------- driving
  geometry_msgs::msg::Twist DriveCommand() {
    geometry_msgs::msg::Twist cmd;
    const auto it = stations_.find(target_station_);
    if (it == stations_.end() || !have_pose_) {
      return cmd;
    }
    const double dx = it->second.x - pose_.x;
    const double dy = it->second.y - pose_.y;
    const double distance = std::hypot(dx, dy);
    if (distance <= goal_tolerance_m_) {
      return cmd;
    }
    const double heading_error = NormalizeAngle(std::atan2(dy, dx) - pose_.yaw);
    cmd.angular.z = std::clamp(3.0 * heading_error, -max_angular_radps_, max_angular_radps_);
    if (std::abs(heading_error) < 0.35) {
      cmd.linear.x = std::clamp(distance * 1.5, 0.1, max_linear_mps_) *
                     std::cos(heading_error);
    }
    return cmd;
  }

  void Integrate(const geometry_msgs::msg::Twist& cmd, const double dt) {
    pose_.yaw = NormalizeAngle(pose_.yaw + cmd.angular.z * dt);
    const double step = cmd.linear.x * dt;
    pose_.x += step * std::cos(pose_.yaw);
    pose_.y += step * std::sin(pose_.yaw);
    twist_ = cmd;
    if (!task_id_.empty()) {
      task_distance_m_ += std::abs(step);
    }
  }

  void PublishOdom() {
    nav_msgs::msg::Odometry odom;
    odom.header.stamp = now();
    odom.header.frame_id = "map";
    odom.child_frame_id = robot_id_ + "/base_footprint";
    odom.pose.pose.position.x = pose_.x;
    odom.pose.pose.position.y = pose_.y;
    odom.pose.pose.orientation = QuaternionFromYaw(pose_.yaw);
    odom.twist.twist = twist_;
    odom_pub_->publish(odom);
  }

  void CheckArrival() {
    const auto it = stations_.find(target_station_);
    if (it == stations_.end() ||
        std::hypot(it->second.x - pose_.x, it->second.y - pose_.y) > goal_tolerance_m_) {
      return;
    }
    current_station_ = target_station_;
    if (!WholeRoute()) {
      // 到达后释放身后的边和上一个路口（即授予过的资源中除当前路口外的全部）。
      ReleaseAllExceptCurrent();
    }
    ++path_index_;
    if (current_station_ == RouteTarget()) {
      if (WholeRoute()) {
        ReleaseAllExceptCurrent();
      }
      UpdateFleetState(task_id_.empty() ? "available" : "busy", current_station_);
      ArrivedAtTarget();
      return;
    }
    if (WholeRoute()) {
      // 整条路径已经拿到，直接开往下一站。
      target_station_ = robot_simulation::NextStation(path_, path_index_);
      return;
    }
    wait_started_t_ = Now();
    last_request_t_ = 0.0;
    phase_ = Phase::kWaitGrant;
    WaitGrant();
  }

  // 释放本车持有（授予应答给过）的资源中除当前路口外的全部。
  void ReleaseAllExceptCurrent() {
    std::vector<std::string> ids;
    for (const auto& id : held_) {
      if (ParseNodeResource(id) != std::optional<std::string>(current_station_)) {
        ids.push_back(id);
      }
    }
    Release(ids);
  }

  // ---------------------------------------------------------------- yield (M3)
  // 当前要去的站点：退避中是避让点，否则是本程目的地。
  const std::string& RouteTarget() const {
    return robot_simulation::RouteTarget(goal_station_, backoff_station_);
  }

  void ArrivedAtTarget() {
    if (!backoff_station_.empty() && current_station_ == backoff_station_) {
      yield_hold_until_ = Now() + backoff_hold_s_;
      phase_ = Phase::kYieldHold;
      return;
    }
    LegArrived();
  }

  void YieldHold() {
    if (Now() < yield_hold_until_) {
      return;
    }
    backoff_station_.clear();
    phase_ = Phase::kRoute;  // 从避让点重新寻路去原目的地
  }

  void ClearYieldState() {
    avoid_edges_.clear();
    avoid_nodes_.clear();
    backoff_station_.clear();
    pending_yield_.reset();
  }

  // 路径首站是当前站点、相邻两站之间在路网里有边。
  bool ValidPath(const std::vector<std::string>& path) const {
    if (path.size() < 2U || path.front() != current_station_) {
      return false;
    }
    for (std::size_t i = 1; i < path.size(); ++i) {
      const auto it = graph_.find(path[i - 1]);
      if (it == graph_.end() ||
          std::none_of(it->second.begin(), it->second.end(),
                       [&](const DirectedEdge& e) { return e.to == path[i]; })) {
        return false;
      }
    }
    return true;
  }

  void ApplyYield(const robot_interfaces_mission::msg::TrafficYield& msg) {
    backoff_station_ =
        robot_simulation::BackoffStationAfterYield(msg.action, msg.station_path, backoff_station_);
    if (msg.action == "reroute") {
      for (const auto& id : msg.avoid_resource_ids) {
        if (const auto edge = ParseEdgeResource(id)) {
          avoid_edges_.insert(*edge);
        } else if (const auto node = ParseNodeResource(id)) {
          if (*node != current_station_) {
            avoid_nodes_.insert(*node);
          }
        }
      }
      path_ = ValidPath(msg.station_path) && msg.station_path.back() == goal_station_
                  ? msg.station_path
                  : ShortestPath(current_station_, goal_station_);
    } else if (msg.action == "backoff") {
      if (msg.station_path.empty()) {
        return;
      }
      backoff_hold_s_ = msg.hold_s;
      path_ = ValidPath(msg.station_path) ? msg.station_path
                                          : ShortestPath(current_station_, backoff_station_);
    } else {
      return;
    }
    if (path_.size() < 2U) {
      RCLCPP_WARN(get_logger(), "%s: yield %s has no usable path; staying", robot_id_.c_str(),
                  msg.action.c_str());
      backoff_station_.clear();
      phase_ = Phase::kRoute;
      return;
    }
    path_index_ = 1;
    last_request_t_ = 0.0;
    grant_hint_ = true;
    phase_ = Phase::kWaitGrant;
  }

  void LegArrived() {
    ClearYieldState();
    switch (goal_) {
      case Goal::kPickup:
      case Goal::kDropoff:
        dwell_until_ = Now() + dwell_s_;
        phase_ = Phase::kDwell;
        return;
      case Goal::kHome:
      case Goal::kNone:
        goal_ = Goal::kNone;
        phase_ = Phase::kIdle;
        return;
    }
  }

  void Dwell() {
    if (Now() < dwell_until_) {
      return;
    }
    if (goal_ == Goal::kPickup) {
      UpdateTaskState("RUNNING", "loaded at " + pickup_);
      goal_ = Goal::kDropoff;
      goal_station_ = dropoff_;
      phase_ = Phase::kRoute;
      return;
    }
    // 卸货完成：任务结束，回报统计。
    const double finished = Now();
    std::ostringstream message;
    message.setf(std::ios::fixed);
    message.precision(2);
    message << "finished by " << robot_id_ << " duration_s=" << (finished - task_started_t_)
            << " wait_route_s=" << task_wait_s_ << " distance_m=" << task_distance_m_;
    UpdateTaskState("FINISHED", message.str());
    completed_any_task_ = true;
    std::ostringstream event;
    event.setf(std::ios::fixed);
    event.precision(3);
    event << "{\"robot\":\"" << robot_id_ << "\",\"task\":\"" << task_id_ << "\",\"pickup\":\""
          << pickup_ << "\",\"dropoff\":\"" << dropoff_ << "\",\"mode\":\"" << traffic_mode_
          << "\",\"started\":" << task_started_t_ << ",\"finished\":" << finished
          << ",\"duration_s\":" << (finished - task_started_t_) << ",\"wait_route_s\":"
          << task_wait_s_ << ",\"distance_m\":" << task_distance_m_ << "}";
    std_msgs::msg::String msg;
    msg.data = event.str();
    events_pub_->publish(msg);
    task_id_.clear();
    goal_ = Goal::kNone;
    UpdateFleetState("available", current_station_);
    phase_ = Phase::kIdle;
  }

  // ---------------------------------------------------------------- reporting
  void UpdateTaskState(const std::string& state, const std::string& message) {
    if (task_id_.empty()) {
      return;
    }
    auto req = std::make_shared<robot_interfaces_fleet::srv::UpdateRemoteTaskState::Request>();
    req->task_id = task_id_;
    req->state = state;
    req->message = message;
    task_state_client_->async_send_request(req);
  }

  void UpdateFleetState(const std::string& state, const std::string& station) {
    auto req = std::make_shared<robot_interfaces_fleet::srv::UpdateFleetRobotState::Request>();
    req->robot_id = robot_id_;
    req->enabled = true;
    req->state = state;
    req->battery_voltage = 24.0;
    req->current_station_id = station;
    req->update_enabled = true;
    req->update_state = true;
    req->update_battery_voltage = true;
    req->update_current_station_id = true;
    fleet_state_client_->async_send_request(req);
  }

  // ---------------------------------------------------------------- members
  std::string robot_id_;
  std::string topic_namespace_;
  std::string initial_station_id_;
  std::string home_station_id_;
  bool kinematic_sim_ = true;
  std::string traffic_mode_;
  double max_linear_mps_ = 0.8;
  double max_angular_radps_ = 1.8;
  double goal_tolerance_m_ = 0.08;
  double dwell_s_ = 3.0;
  double whole_route_retry_s_ = 2.0;
  double rolling_rerequest_s_ = 2.0;
  bool return_home_when_idle_ = true;
  // 至少完成过一个任务后，空闲时才回停车位（见 agent_idle.hpp）。
  bool completed_any_task_ = false;
  double control_period_s_ = 0.05;

  Phase phase_ = Phase::kLoadMap;
  Goal goal_ = Goal::kNone;
  std::map<std::string, StationPose> stations_;
  std::map<std::string, std::vector<DirectedEdge>> graph_;
  std::set<EdgeKey> blocked_edges_;
  // M3：本车持有的资源（来自授予应答）；改道时临时绕开的路段与路口；退避目标。
  std::set<std::string> held_;
  std::set<EdgeKey> avoid_edges_;
  std::set<std::string> avoid_nodes_;
  std::string backoff_station_;
  double backoff_hold_s_ = 0.0;
  double yield_hold_until_ = 0.0;
  bool paused_ = false;
  std::optional<robot_interfaces_mission::msg::TrafficYield> pending_yield_;
  std::string current_station_;
  std::string goal_station_;
  std::string target_station_;
  std::vector<std::string> path_;
  std::size_t path_index_ = 1;
  std::string task_id_;
  std::string pickup_;
  std::string dropoff_;
  int priority_ = 0;
  double task_started_t_ = 0.0;
  double task_wait_s_ = 0.0;
  double task_distance_m_ = 0.0;
  double total_wait_s_ = 0.0;
  double wait_started_t_ = 0.0;
  double dwell_until_ = 0.0;
  double last_request_t_ = 0.0;
  double retry_jitter_ = 0.0;
  bool grant_hint_ = false;
  bool replan_requested_ = false;
  bool request_pending_ = false;
  bool tasks_pending_ = false;
  bool stations_pending_ = false;
  bool routes_pending_ = false;
  double last_map_request_ = 0.0;
  double last_route_request_ = 0.0;
  double last_claim_ = 0.0;
  double last_task_poll_ = 0.0;
  double last_tick_ = 0.0;
  double last_odom_pub_ = 0.0;
  Pose2D pose_;
  bool have_pose_ = false;
  geometry_msgs::msg::Twist twist_;
  std::mt19937 rng_{std::random_device{}()};

  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr events_pub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<robot_interfaces_mission::msg::TrafficGrant>::SharedPtr grant_sub_;
  rclcpp::Subscription<robot_interfaces_mission::msg::TrafficState>::SharedPtr traffic_state_sub_;
  rclcpp::Subscription<robot_interfaces_mission::msg::TrafficYield>::SharedPtr yield_sub_;
  rclcpp::Client<robot_interfaces_mission::srv::ListStations>::SharedPtr stations_client_;
  rclcpp::Client<robot_interfaces_mission::srv::ListStationRoutes>::SharedPtr routes_client_;
  rclcpp::Client<robot_interfaces_fleet::srv::ListRemoteTasks>::SharedPtr tasks_client_;
  rclcpp::Client<robot_interfaces_fleet::srv::UpdateRemoteTaskState>::SharedPtr task_state_client_;
  rclcpp::Client<robot_interfaces_fleet::srv::UpdateFleetRobotState>::SharedPtr fleet_state_client_;
  rclcpp::Client<robot_interfaces_mission::srv::RequestRouteSegment>::SharedPtr request_client_;
  rclcpp::Client<robot_interfaces_mission::srv::ReleaseRouteSegment>::SharedPtr release_client_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  // 先用一个临时节点读出 mode 参数，再构造对应模式的节点。
  std::string mode = "follow_fleet_station";
  {
    auto probe = std::make_shared<rclcpp::Node>(
        "fleet_robot_agent_mode_probe",
        rclcpp::NodeOptions()
            .allow_undeclared_parameters(true)
            .automatically_declare_parameters_from_overrides(true));
    rclcpp::Parameter value;
    if (probe->get_parameter("mode", value)) {
      mode = value.as_string();
    }
  }
  rclcpp::NodeOptions options;
  options.allow_undeclared_parameters(true);
  rclcpp::Node::SharedPtr node;
  if (mode == "task_agent") {
    node = std::make_shared<FleetTaskAgent>(options);
  } else {
    node = std::make_shared<FleetStationFollower>(options);
  }
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
