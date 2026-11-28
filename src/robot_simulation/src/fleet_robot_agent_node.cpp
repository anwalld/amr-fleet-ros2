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
//     查站点、路网、任务和申请路权 5 秒没有应答就按原样重发（见 agent_request.hpp）。
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces_fleet/srv/control_remote_fleet_task.hpp"
#include "robot_interfaces_fleet/srv/list_fleet_robots.hpp"
#include "robot_interfaces_fleet/srv/list_remote_tasks.hpp"
#include "robot_simulation/task_stop_observation.hpp"
#include "robot_simulation/parked_task_checkpoint.hpp"
#include "robot_simulation/fleet_sim_stop.hpp"
#include "robot_simulation/agent_idle.hpp"
#include "robot_simulation/agent_lease.hpp"
#include "robot_simulation/agent_request.hpp"
#include "robot_simulation/agent_route.hpp"
#include "robot_simulation/agent_yield.hpp"
#include "robot_interfaces_fleet/srv/update_remote_task_state.hpp"
#include "robot_interfaces_mission/msg/traffic_grant.hpp"
#include "robot_interfaces_mission/msg/traffic_state.hpp"
#include "robot_interfaces_mission/msg/traffic_yield.hpp"
#include "robot_interfaces_mission/srv/list_station_routes.hpp"
#include "robot_interfaces_mission/srv/release_route_segment.hpp"
#include "robot_interfaces_mission/srv/request_route_segment.hpp"
#include "std_msgs/msg/bool.hpp"
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
    if (!ShouldPoll(last_station_poll_, 2s)) {
      return;
    }
    auto request = std::make_shared<robot_interfaces_mission::srv::ListStations::Request>();
    stations_read_.Poll(
        stations_client_, request, [this](const auto& response) {
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
    if (!ShouldPoll(last_fleet_poll_, 750ms)) {
      return;
    }
    auto request = std::make_shared<robot_interfaces_fleet::srv::ListFleetRobots::Request>();
    request->include_disabled = true;
    fleet_read_.Poll(
        fleet_client_, request, [this](const auto& response) {
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
    arrival_report_call_.Cancel();
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
    if (arrival_reported_ || target_station_id_.empty()) {
      return;
    }
    const std::string target_station = target_station_id_;
    auto request = std::make_shared<robot_interfaces_fleet::srv::UpdateFleetRobotState::Request>();
    request->robot_id = robot_id_;
    request->state = "available";
    request->current_station_id = target_station;
    request->update_state = true;
    request->update_current_station_id = true;
    arrival_report_call_.Poll(
        update_fleet_client_, request, [this, target_station](const auto& response) {
          if (target_station == target_station_id_ && response->success) {
            arrival_reported_ = true;
          }
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
  robot_simulation::RetryingRequest<rclcpp::Client<robot_interfaces_mission::srv::ListStations>> stations_read_;
  robot_simulation::RetryingRequest<rclcpp::Client<robot_interfaces_fleet::srv::ListFleetRobots>> fleet_read_;
  robot_simulation::RetryingRequest<rclcpp::Client<robot_interfaces_fleet::srv::UpdateFleetRobotState>>
      arrival_report_call_;
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

using robot_simulation::DirectedEdge;
using robot_simulation::EdgeKey;
using robot_simulation::MakeEdgeKey;

// 资源号只由路权服务生成（规格里写明了格式）。代理只解析，不拼接：
//   route_edge:<a>__<b> → 无序站点对；route_node:<站点> → 站点。
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
    const bool stop_enabled = declare_parameter<bool>("fleet_sim_stop_enabled", false);
    remote_control_enabled_ = declare_parameter<bool>("remote_task_control_enabled", false);
    if (stop_enabled && !kinematic_sim_) {
      throw std::invalid_argument("fleet_sim_stop_enabled requires kinematic_sim");
    }
    if (stop_enabled || remote_control_enabled_) {
      fleet_stop_sub_ = create_subscription<std_msgs::msg::Bool>(
          "/fleet/sim_stop/state", rclcpp::QoS(1).reliable().transient_local(),
          [this](std_msgs::msg::Bool::ConstSharedPtr msg) {
            fleet_stopped_ = msg->data;
            fleet_stop_seen_s_ = Now();
          });
    }
    traffic_mode_ = declare_parameter<std::string>("traffic_reservation_mode", "rolling");
    max_linear_mps_ = declare_parameter<double>("max_linear_mps", 0.8);
    max_angular_radps_ = declare_parameter<double>("max_angular_radps", 1.8);
    goal_tolerance_m_ = declare_parameter<double>("goal_tolerance_m", 0.08);
    dwell_s_ = declare_parameter<double>("dwell_s", 3.0);
    whole_route_retry_s_ = declare_parameter<double>("whole_route_retry_s", 2.0);
    rolling_rerequest_s_ = declare_parameter<double>("rolling_rerequest_s", 2.0);
    return_home_when_idle_ = declare_parameter<bool>("return_home_when_idle", true);
    control_period_s_ = declare_parameter<double>("control_period_s", 0.05);

    if (remote_control_enabled_ && !kinematic_sim_) {
      throw std::invalid_argument("remote task control requires kinematic simulation");
    }
    if (remote_control_enabled_) {
      const auto checkpoint_path = declare_parameter<std::string>("restart_checkpoint_path", "");
      checkpoint_enabled_ = !checkpoint_path.empty();
      if (checkpoint_enabled_) {
        using Start = robot_simulation::ParkedTaskCheckpointStore::Start;
        const auto status = parked_store_.Open(checkpoint_path);
        restart_recovery_required_ = status != Start::kFresh;
        if (status == Start::kExisting) {
          parked_checkpoint_ = parked_store_.Read();
          if (parked_checkpoint_ && (parked_checkpoint_->robot_id != robot_id_ ||
                                    parked_checkpoint_->mode != traffic_mode_)) {
            parked_checkpoint_.reset();
          }
        }
        if (restart_recovery_required_) {
          RCLCPP_WARN(get_logger(), "agent motion inhibited pending restart reconciliation: %s",
                      checkpoint_path.c_str());
        }
      }
      control_service_ = create_service<RemoteControl>(
          "/v2/remote_task_control/" + robot_id_,
          [this](const std::shared_ptr<RemoteControl::Request> request,
                 std::shared_ptr<RemoteControl::Response> response) {
            HandleRemoteControl(*request, *response);
          });
    }

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
          if (msg->event == "replan" && lease_tracker_.IsCurrentEpoch(msg->service_epoch)) {
            replan_requested_ = true;
            grant_hint_ = true;
          } else if (msg->event == "granted") {
            if (phase_ == Phase::kWaitGrant) {
              grant_hint_ = grant_hint_ || lease_tracker_.CanHintGrant(msg->service_epoch);
            } else if (msg->resource_ids.size() == msg->resource_revisions.size()) {
              std::vector<std::string> unused;
              for (std::size_t i = 0; i < msg->resource_ids.size(); ++i) {
                const auto& id = msg->resource_ids[i];
                if (lease_tracker_.AcceptUnneededGrant(
                        msg->service_epoch, id, msg->resource_revisions[i]) &&
                    ParseNodeResource(id) != std::optional<std::string>(current_station_)) {
                  unused.push_back(id);
                }
              }
              Release(unused);
            }
          }
        });
    traffic_state_sub_ = create_subscription<robot_interfaces_mission::msg::TrafficState>(
        "/traffic/state", 10, [this](robot_interfaces_mission::msg::TrafficState::ConstSharedPtr msg,
                               const rclcpp::MessageInfo& info) {
          std::set<EdgeKey> blocked;
          std::set<std::string> owned;
          for (std::size_t i = 0; i < msg->resource_ids.size() && i < msg->owner_ids.size(); ++i) {
            if (msg->owner_ids[i].rfind("traffic_block:", 0) == 0) {
              if (const auto edge = ParseEdgeResource(msg->resource_ids[i])) {
                blocked.insert(*edge);
              }
            }
            if (msg->owner_ids[i] == robot_id_) {
              owned.insert(msg->resource_ids[i]);
            }
          }
          blocked_edges_ = std::move(blocked);
          if (paused_ && owned.empty() && !MotionHeldForTaskControl()) {
            // 兜底暂停后运维清除了本车的路权：恢复，先重新占住所在路口。
            paused_ = false;
            lease_tracker_.ClearHeld();
            phase_ = Phase::kReclaim;
            RCLCPP_WARN(get_logger(), "%s: resumed after deadlock pause", robot_id_.c_str());
          }
          observed_owned_ = std::move(owned);
          observed_traffic_mode_ = msg->mode;
          const auto& gid = info.get_rmw_message_info().publisher_gid;
          constexpr char hex[] = "0123456789abcdef";
          observed_traffic_authority_.clear();
          for (const auto byte : gid.data) {
            observed_traffic_authority_.push_back(hex[byte >> 4]);
            observed_traffic_authority_.push_back(hex[byte & 0xf]);
          }
          traffic_seen_s_ = Now();
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
  enum class Goal { kNone, kPickup, kDropoff, kHome, kRelocate };

  struct StationPose {
    double x = 0.0;
    double y = 0.0;
    double yaw = 0.0;
  };

  double Now() const { return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count(); }
  bool WholeRoute() const { return traffic_mode_ == "whole_route"; }

  using RemoteControl = robot_interfaces_fleet::srv::ControlRemoteFleetTask;

  struct TaskControlReceipt {
    std::string task_id;
    std::uint8_t operation;
    std::string destination_station;
    std::string result;
    std::string message;
  };

  bool FleetStopped(const double t) const {
    return fleet_stop_sub_ &&
        robot_simulation::FleetSimMotionStopped(fleet_stopped_, fleet_stop_seen_s_, t);
  }

  bool MotionHeldForTaskControl() const { return active_stop_revision_ || canceled_hold_; }

  bool OwnsMotionResources(const double t) const {
    const auto& held = lease_tracker_.held();
    return traffic_seen_s_ && t - *traffic_seen_s_ <= 2.5 && !held.empty() &&
        std::includes(observed_owned_.begin(), observed_owned_.end(), held.begin(), held.end());
  }

  void HandleRemoteControl(
      const RemoteControl::Request& request, RemoteControl::Response& response) {
    const bool identifies_current = request.task_id == task_id_ ||
        (canceled_hold_ && request.task_id == canceled_task_id_);
    const auto execution = request.execution_id == 0 && identifies_current
                               ? task_execution_id_ : request.execution_id;
    response.task_id = request.task_id;
    response.execution_id = execution;
    response.revision = request.revision;
    if (restart_recovery_required_) {
      response.result = "RECOVERY_REQUIRED";
      response.message = "vehicle state and occupied route need restart reconciliation";
      return;
    }
    const bool active = !task_id_.empty() && request.task_id == task_id_ &&
                        execution == task_execution_id_;
    const bool parked = canceled_hold_ && request.task_id == canceled_task_id_ &&
                        execution == task_execution_id_;
    if (active || parked) {
      response.latest_revision = last_control_revision_;
    }
    const auto key = std::make_pair(execution, request.revision);
    auto prior = control_receipts_.find(key);
    if (prior != control_receipts_.end()) {
      if (prior->second.task_id != request.task_id ||
          (request.operation != RemoteControl::Request::QUERY &&
           (prior->second.operation != request.operation ||
            prior->second.destination_station != request.destination_station))) {
        response.result = "REJECTED";
        response.message = "control revision already belongs to another request";
        return;
      }
      response.result = prior->second.result;
      response.message = prior->second.message;
      response.success = response.result != "REJECTED" && response.result != "SUPERSEDED";
      return;
    }
    if (request.operation == RemoteControl::Request::QUERY) {
      response.success = (active || parked) && request.revision == 0;
      response.result = !response.success ? "UNKNOWN" :
          (parked ? "AWAITING_MOVE" : (MotionHeldForTaskControl() ? "HELD" : "RUNNING"));
      response.message = response.success ? "current execution" : "control receipt unknown";
      return;
    }
    if ((!active && !(parked && request.operation == RemoteControl::Request::MOVE)) ||
        request.execution_id == 0 || request.revision <= last_control_revision_) {
      response.result = "REJECTED";
      response.message = "task execution or revision changed";
      return;
    }

    last_control_revision_ = request.revision;
    response.latest_revision = last_control_revision_;
    auto& receipt = control_receipts_.emplace(
        key, TaskControlReceipt{request.task_id, request.operation, request.destination_station,
                                "REJECTED", "operation unavailable"})
                        .first->second;
    if (parked_checkpoint_ && checkpoint_enabled_) {
      // Persist the revision even when the request will be rejected. A delayed
      // retry after restart must not turn a spent move request into motion.
      auto updated = *parked_checkpoint_;
      updated.latest_revision = last_control_revision_;
      if (!parked_store_.Save(updated)) {
        receipt.message = "cannot persist parked control revision";
        response.result = receipt.result;
        response.message = receipt.message;
        return;
      }
      parked_checkpoint_ = std::move(updated);
    }
    if (request.operation == RemoteControl::Request::MOVE && parked) {
      RequestRelocation(request.destination_station, receipt);
    } else if (request.operation == RemoteControl::Request::RESUME) {
      if (FleetStopped(Now())) {
        receipt.message = "fleet stop active or unavailable";
      } else if (!active_stop_revision_ || !stop_confirmed_) {
        receipt.message = "task has no confirmed pause";
      } else if (paused_ || !OwnsMotionResources(Now())) {
        receipt.message = "task must retain its motion resources before resuming";
      } else if (parked_checkpoint_ && !ParkedResourcesMatch(*parked_checkpoint_)) {
        receipt.message = "saved stop no longer matches live traffic authority";
      } else if (!InvalidateStopCheckpoint()) {
        receipt.message = "cannot invalidate stop checkpoint before resuming";
      } else {
        if (phase_ == Phase::kDwell && dwell_frozen_at_) {
          dwell_until_ += Now() - *dwell_frozen_at_;
        }
        active_stop_revision_.reset();
        dwell_frozen_at_.reset();
        stop_confirmed_ = false;
        receipt.result = "RESUMED";
        receipt.message = "task allowed to resume";
        UpdateTaskState("RUNNING", receipt.message);
      }
    } else if (request.operation == RemoteControl::Request::PAUSE ||
               request.operation == RemoteControl::Request::CANCEL) {
      if (active_stop_revision_ &&
          control_receipts_.at({task_execution_id_, *active_stop_revision_}).operation ==
              RemoteControl::Request::CANCEL) {
        receipt.message = "task cancellation already in progress";
      } else if (!InvalidateStopCheckpoint()) {
        receipt.message = "cannot invalidate previous stop checkpoint";
      } else {
        if (active_stop_revision_ && !stop_confirmed_) {
          auto& previous = control_receipts_.at({task_execution_id_, *active_stop_revision_});
          previous.result = "SUPERSEDED";
          previous.message = "replaced by a newer stop request";
        }
        if (!active_stop_revision_) {
          dwell_frozen_at_ = Now();
        }
        active_stop_revision_ = request.revision;
        stop_confirmed_ = false;
        stop_observation_.Reset();
        const bool cancel = request.operation == RemoteControl::Request::CANCEL;
        receipt.result = cancel ? "CANCELING" : "PAUSING";
        receipt.message = "waiting for stationary odometry";
      }
    }
    response.result = receipt.result;
    response.message = receipt.message;
    response.success = receipt.result != "REJECTED";
  }

  void RequestRelocation(const std::string& destination, TaskControlReceipt& receipt) {
    const bool mid_segment = phase_ == Phase::kDrive;
    const auto& exit_station = mid_segment ? target_station_ : current_station_;
    if (FleetStopped(Now()) || paused_ || !OwnsMotionResources(Now()) || route_call_.pending()) {
      receipt.message = "relocation waiting for fresh stop and route state";
      return;
    }
    if (destination.empty() || (destination == exit_station && !mid_segment) ||
        stations_.count(destination) == 0U ||
        (destination != exit_station && ShortestPath(exit_station, destination).size() < 2U)) {
      receipt.message = "destination must be reachable from the next safe exit";
      return;
    }
    if (parked_checkpoint_ && !ParkedResourcesMatch(*parked_checkpoint_)) {
      receipt.message = "saved stop no longer matches live traffic authority";
      return;
    }
    if (!InvalidateStopCheckpoint()) {
      receipt.message = "cannot invalidate parked checkpoint before relocation";
      return;
    }
    ClearYieldState();
    goal_ = Goal::kRelocate;
    relocation_destination_ = destination;
    if (mid_segment) {
      // Finish the occupied edge under the existing grant before planning from
      // its exit; retain the edge lease until CheckArrival releases it.
      goal_station_ = exit_station;
      path_ = {current_station_, exit_station};
      path_index_ = 1;
    } else {
      goal_station_ = destination;
      phase_ = Phase::kRoute;
    }
    canceled_hold_ = false;
    receipt.result = "MOVE_ACCEPTED";
    receipt.message = "independent relocation accepted";
  }

  bool InvalidateStopCheckpoint() {
    if (parked_checkpoint_ && !parked_store_.Invalidate()) {
      return false;
    }
    parked_checkpoint_.reset();
    return true;
  }

  std::optional<robot_simulation::PausedTaskProgress> CapturePausedProgress() const {
    if ((goal_ != Goal::kPickup && goal_ != Goal::kDropoff) ||
        (phase_ != Phase::kDrive && phase_ != Phase::kDwell) || paused_ ||
        pending_yield_ || replan_requested_ || !backoff_station_.empty() ||
        !avoid_nodes_.empty() || !avoid_edges_.empty()) {
      return std::nullopt;
    }
    robot_simulation::PausedTaskProgress progress;
    progress.pickup_leg = goal_ == Goal::kPickup;
    progress.pickup = pickup_;
    progress.dropoff = dropoff_;
    progress.priority = priority_;
    if (phase_ == Phase::kDrive) {
      progress.path = path_;
      progress.path_index = path_index_;
    } else {
      progress.dwell_remaining = std::max(0.0, dwell_until_ - dwell_frozen_at_.value_or(Now()));
    }
    return progress;
  }

  void ObserveTaskStopped(const double t) {
    if (!active_stop_revision_ || stop_confirmed_ ||
        !stop_observation_.Observe(t, pose_.x, pose_.y)) {
      return;
    }
    auto& receipt = control_receipts_.at({task_execution_id_, *active_stop_revision_});
    const bool cancel = receipt.operation == RemoteControl::Request::CANCEL;
    const auto progress = !cancel && checkpoint_enabled_ ? CapturePausedProgress() : std::nullopt;
    if (checkpoint_enabled_ && (cancel || progress.has_value())) {
      const auto exit_station = phase_ == Phase::kDrive ? target_station_ : std::string{};
      robot_simulation::ParkedTaskCheckpoint checkpoint{
          robot_id_, traffic_mode_, task_id_, current_station_, exit_station, task_execution_id_,
          *active_stop_revision_, last_control_revision_, pose_.x, pose_.y, pose_.yaw,
          lease_tracker_.held(),
          completed_any_task_, observed_traffic_authority_, progress};
      if (!parked_store_.Save(checkpoint)) {
        RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "cannot persist task stop");
        return;
      }
      parked_checkpoint_ = std::move(checkpoint);
    }
    stop_confirmed_ = true;
    receipt.result = cancel ? "CANCELED" : "PAUSED";
    receipt.message = "stationary odometry confirmed";
    UpdateTaskState(receipt.result, receipt.message);
    if (cancel) {
      canceled_hold_ = true;
      canceled_task_id_ = task_id_;
      task_id_.clear();
      active_stop_revision_.reset();
    }
  }

  bool CheckParkedLocation(const robot_simulation::ParkedTaskCheckpoint& saved) const {
    const auto from = stations_.find(saved.station);
    if (from == stations_.end()) {
      return false;
    }
    const auto& start = from->second;
    if (saved.exit_station.empty()) {
      return std::hypot(saved.x - start.x, saved.y - start.y) <= goal_tolerance_m_ &&
          std::any_of(saved.resources.begin(), saved.resources.end(), [&](const auto& id) {
            return ParseNodeResource(id) == saved.station;
          });
    }
    const auto exit = stations_.find(saved.exit_station);
    const auto edges = graph_.find(saved.station);
    if (exit == stations_.end() || edges == graph_.end() ||
        std::none_of(edges->second.begin(), edges->second.end(), [&](const auto& edge) {
          return edge.to == saved.exit_station;
        })) {
      return false;
    }
    const auto physical_edge = std::minmax(saved.station, saved.exit_station);
    if (std::none_of(saved.resources.begin(), saved.resources.end(), [&](const auto& id) {
          const auto edge = ParseEdgeResource(id);
          return edge && edge->first == physical_edge.first && edge->second == physical_edge.second;
        })) {
      return false;
    }
    const double dx = exit->second.x - start.x;
    const double dy = exit->second.y - start.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= 0.0) {
      return false;
    }
    const double t = std::clamp(((saved.x - start.x) * dx + (saved.y - start.y) * dy) /
                                    length_squared, 0.0, 1.0);
    return std::hypot(saved.x - start.x - t * dx, saved.y - start.y - t * dy) <=
        goal_tolerance_m_;
  }

  bool ParkedResourcesMatch(const robot_simulation::ParkedTaskCheckpoint& saved) const {
    return traffic_seen_s_ && Now() - *traffic_seen_s_ <= 2.5 &&
        observed_traffic_mode_ == saved.mode && observed_owned_ == saved.resources &&
        observed_traffic_authority_ == saved.traffic_authority;
  }

  bool CheckPausedPath(const robot_simulation::ParkedTaskCheckpoint& saved) const {
    if (!saved.paused) {
      return true;
    }
    const auto& progress = *saved.paused;
    const auto& destination = progress.pickup_leg ? progress.pickup : progress.dropoff;
    if (saved.exit_station.empty()) {
      return progress.path.empty() && saved.station == destination;
    }
    if (progress.path.empty() || progress.path_index == 0 ||
        progress.path_index >= progress.path.size() || progress.path.back() != destination ||
        progress.path[progress.path_index - 1] != saved.station ||
        progress.path[progress.path_index] != saved.exit_station) {
      return false;
    }
    for (std::size_t i = progress.path_index; i < progress.path.size(); ++i) {
      const auto edge = graph_.find(progress.path[i - 1]);
      if (edge == graph_.end() ||
          std::none_of(edge->second.begin(), edge->second.end(), [&](const auto& candidate) {
            return candidate.to == progress.path[i];
          })) {
        return false;
      }
    }
    return true;
  }

  void ReconcileParkedTask() {
    if (!parked_checkpoint_) {
      return;
    }
    LoadMap();
    ResendOverdueRequests(Now());
    const auto& saved = *parked_checkpoint_;
    if (stations_.empty() || graph_.empty() || !CheckParkedLocation(saved) ||
        !CheckPausedPath(saved) || !ParkedResourcesMatch(saved) || !tasks_client_->service_is_ready() ||
        tasks_call_.pending() || !t_ok(last_task_poll_, 0.5)) {
      return;
    }
    auto request = std::make_shared<robot_interfaces_fleet::srv::ListRemoteTasks::Request>();
    request->robot_id = robot_id_;
    request->state = saved.paused ? "PAUSED" : "CANCELED";
    tasks_call_.Send(tasks_client_, request, [this](const auto& reply) {
      if (!reply->success || !restart_recovery_required_ || !parked_checkpoint_) {
        return;
      }
      const auto& saved = *parked_checkpoint_;
      const auto task = std::find(reply->task_ids.begin(), reply->task_ids.end(), saved.task_id);
      if (task == reply->task_ids.end() || !ParkedResourcesMatch(saved)) {
        return;
      }
      const auto index = static_cast<std::size_t>(task - reply->task_ids.begin());
      if (index >= reply->assigned_robot_ids.size() || index >= reply->states.size() ||
          reply->assigned_robot_ids[index] != robot_id_ ||
          reply->states[index] != (saved.paused ? "PAUSED" : "CANCELED")) {
        return;
      }
      pose_ = {saved.x, saved.y, saved.yaw};
      have_pose_ = true;
      current_station_ = saved.station;
      target_station_ = saved.exit_station;
      phase_ = target_station_.empty() ? Phase::kIdle : Phase::kDrive;
      completed_any_task_ = saved.completed_task;
      task_execution_id_ = saved.execution;
      last_control_revision_ = saved.latest_revision;
      if (saved.paused) {
        const auto& progress = *saved.paused;
        if (index >= reply->pickup_stations.size() || index >= reply->dropoff_stations.size() ||
            reply->pickup_stations[index] != progress.pickup ||
            reply->dropoff_stations[index] != progress.dropoff) {
          return;
        }
        task_id_ = saved.task_id;
        pickup_ = progress.pickup;
        dropoff_ = progress.dropoff;
        priority_ = progress.priority;
        goal_ = progress.pickup_leg ? Goal::kPickup : Goal::kDropoff;
        goal_station_ = progress.pickup_leg ? pickup_ : dropoff_;
        path_ = progress.path;
        path_index_ = progress.path_index;
        task_started_t_ = Now();
        if (target_station_.empty()) {
          phase_ = Phase::kDwell;
          dwell_frozen_at_ = Now();
          dwell_until_ = *dwell_frozen_at_ + progress.dwell_remaining;
        }
        active_stop_revision_ = saved.stop_revision;
        stop_confirmed_ = true;
        control_receipts_.emplace(std::make_pair(saved.execution, saved.stop_revision),
            TaskControlReceipt{saved.task_id, RemoteControl::Request::PAUSE, "", "PAUSED",
                               "stationary odometry confirmed"});
      } else {
        canceled_task_id_ = saved.task_id;
        canceled_hold_ = true;
        control_receipts_.emplace(std::make_pair(saved.execution, saved.stop_revision),
            TaskControlReceipt{saved.task_id, RemoteControl::Request::CANCEL, "", "CANCELED",
                               "stationary odometry confirmed"});
      }
      lease_tracker_.RestoreReconciledResources(saved.resources);
      restart_recovery_required_ = false;
      RCLCPP_INFO(get_logger(), "reconciled task stop for %s at %s",
                  saved.task_id.c_str(), current_station_.c_str());
    }, Now());
  }

  // ---------------------------------------------------------------- main loop
  void Tick() {
    const double t = Now();
    const double dt = last_tick_ > 0.0 ? std::min(t - last_tick_, 0.2) : 0.0;
    last_tick_ = t;

    if (restart_recovery_required_) {
      ReconcileParkedTask();
      return;
    }
    const bool fleet_stopped = FleetStopped(t);
    const bool task_held = MotionHeldForTaskControl();
    geometry_msgs::msg::Twist cmd;
    if (!fleet_stopped && !task_held && phase_ == Phase::kDrive) {
      cmd = DriveCommand();
    }
    if (kinematic_sim_ && have_pose_) {
      Integrate(cmd, dt);
    } else if (!kinematic_sim_) {
      cmd_pub_->publish(cmd);
    }
    if (have_pose_ && t - last_odom_pub_ >= 0.1) {
      PublishOdom();
      ObserveTaskStopped(t);
      last_odom_pub_ = t;
    }
    if (!fleet_stopped || task_held) {
      ResendOverdueRequest(release_call_, "/v2/release_route_segment", t);
      ResendOverdueRequest(fleet_state_call_, "/v2/update_fleet_robot_state", t);
      ResendOverdueRequest(task_state_call_, "/v2/update_remote_task_state", t);
      FlushPendingWrites(t);
      FlushPendingReleases(t);
    }
    if (fleet_stopped) {
      return;
    }
    ResendOverdueRequests(t);
    if (task_held) {
      return;
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

  // 与阶段无关：换了阶段，在途请求也还要等到应答（否则 pending 永远不清）。
  template <typename RequestT>
  void ResendOverdueRequest(RequestT& call, const char* service, const double t) {
    if (call.ResendIfOverdue(t)) {
      RCLCPP_WARN(get_logger(), "%s: no response from %s for %.0fs, resending",
                  robot_id_.c_str(), service, robot_simulation::kServiceResponseTimeoutS);
    }
  }

  void ResendOverdueRequests(const double t) {
    ResendOverdueRequest(stations_call_, "/v2/list_stations", t);
    ResendOverdueRequest(routes_call_, "/v2/list_station_routes", t);
    ResendOverdueRequest(tasks_call_, "/v2/list_remote_tasks", t);
    ResendOverdueRequest(route_call_, "/v2/request_route_segment", t);
  }

  // ---------------------------------------------------------------- map
  void LoadMap() {
    if (!stations_client_->service_is_ready() || !routes_client_->service_is_ready()) {
      return;
    }
    if (!stations_call_.pending() && stations_.empty() && t_ok(last_map_request_, 1.0)) {
      stations_call_.Send(
          stations_client_,
          std::make_shared<robot_interfaces_mission::srv::ListStations::Request>(),
          [this](const auto& r) {
            for (std::size_t i = 0; i < r->station_ids.size() && i < r->x.size(); ++i) {
              stations_[r->station_ids[i]] = {r->x[i], r->y[i], i < r->yaw.size() ? r->yaw[i] : 0.0};
            }
          },
          Now());
    }
    if (!routes_call_.pending() && graph_.empty() && t_ok(last_route_request_, 1.0)) {
      routes_call_.Send(
          routes_client_,
          std::make_shared<robot_interfaces_mission::srv::ListStationRoutes::Request>(),
          [this](const auto& r) {
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
          },
          Now());
    }
    if (!restart_recovery_required_ && !stations_.empty() && !graph_.empty()) {
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
    if (!lease_tracker_.pending_releases().empty()) {
      FlushPendingReleases(Now());
      return;
    }
    if (route_call_.pending() || !request_client_->service_is_ready() ||
        !fleet_state_client_->service_is_ready() || !t_ok(last_claim_, 1.0)) {
      return;
    }
    SendRouteRequest("", current_station_, {}, [this](const auto& r) {
      if (!r->granted) {
        return;  // 下一秒重试（rolling 下已在队列里）
      }
      UpdateFleetState("available", current_station_);
      phase_ = Phase::kIdle;
      RCLCPP_INFO(get_logger(), "%s ready at %s", robot_id_.c_str(), current_station_.c_str());
    });
  }

  // ---------------------------------------------------------------- tasks
  void Idle() { PollTasks(); }

  void PollTasks() {
    if (tasks_call_.pending() || !tasks_client_->service_is_ready() || !t_ok(last_task_poll_, 0.5)) {
      return;
    }
    auto req = std::make_shared<robot_interfaces_fleet::srv::ListRemoteTasks::Request>();
    req->robot_id = robot_id_;
    req->state = "DISPATCHED_REMOTE";
    tasks_call_.Send(
        tasks_client_, req,
        [this](const auto& r) {
          if (!r->success || MotionHeldForTaskControl() || FleetStopped(Now())) {
            return;
          }
          const bool heading_home =
              goal_ == Goal::kHome &&
              (phase_ == Phase::kRoute || (phase_ == Phase::kWaitGrant && !route_call_.pending()));
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
        },
        Now());
  }

  // 兜底暂停解除后：重新占住所在路口，再继续本程。
  void Reclaim() {
    if (!lease_tracker_.pending_releases().empty()) {
      FlushPendingReleases(Now());
      return;
    }
    if (route_call_.pending() || !t_ok(last_claim_, 1.0)) {
      return;
    }
    SendRouteRequest("", current_station_, {}, [this](const auto& r) {
      if (!r->granted || phase_ != Phase::kReclaim) {
        return;
      }
      phase_ = goal_ == Goal::kNone ? Phase::kIdle : Phase::kRoute;
    });
  }

  void StartTask(const std::string& task_id, const std::string& pickup,
                 const std::string& dropoff, const int priority) {
    ClearYieldState();
    // A process restart gets a different execution ID even if the dispatcher
    // still lists the same business task.
    task_execution_id_ = execution_rng_();
    if (task_execution_id_ == 0) {
      task_execution_id_ = 1;
    }
    last_control_revision_ = 0;
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
    return robot_simulation::ShortestPath(
        graph_, from, to, blocked_edges_, avoid_edges_, avoid_nodes_);
  }

  void Route() {
    if (!lease_tracker_.pending_releases().empty()) {
      FlushPendingReleases(Now());
      return;
    }
    using robot_simulation::RouteEntryAction;
    switch (robot_simulation::DecideRouteEntryAction(
        paused_, pending_yield_.has_value(), route_call_.pending())) {
      case RouteEntryAction::kWaitPaused:
      case RouteEntryAction::kWaitInFlight:
        return;
      case RouteEntryAction::kApplyYield:
        ApplyYield(*pending_yield_);
        pending_yield_.reset();
        return;
      case RouteEntryAction::kContinue:
        break;
    }
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
    auto plan = robot_simulation::PlanRoute(
        graph_, current_station_, RouteTarget(), blocked_edges_, avoid_edges_, avoid_nodes_);
    if (plan.detours_dropped) {
      RCLCPP_WARN(get_logger(), "%s: temporary detours dropped, they cut off %s -> %s",
                  robot_id_.c_str(), current_station_.c_str(), RouteTarget().c_str());
    }
    path_ = std::move(plan.path);
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
    if (!lease_tracker_.pending_releases().empty()) {
      FlushPendingReleases(Now());
      return;
    }
    if (route_call_.pending() || phase_ != Phase::kWaitGrant || paused_) {
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
    auto req = std::make_shared<robot_interfaces_mission::srv::RequestRouteSegment::Request>();
    req->robot_id = robot_id_;
    req->from_station = from;
    req->to_station = to;
    req->priority = priority_;
    req->station_path = path;
    req->goal_station = goal_station_;
    route_call_.Send(
        request_client_, req,
        [this, on_done = std::move(on_done)](const auto& r) {
          if (lease_tracker_.AcceptRequestResponse(
                  r->service_epoch, r->granted, r->resource_ids, r->resource_revisions)) {
            on_done(r);
          }
        },
        Now());
  }

  void Release(const std::vector<std::string>& ids) {
    if (ids.empty()) {
      return;
    }
    lease_tracker_.QueueRelease(ids);
    FlushPendingReleases(Now());
  }

  void FlushPendingReleases(const double now_s) {
    const auto& pending = lease_tracker_.pending_releases();
    if (pending.empty() || release_call_.pending() || !release_client_->service_is_ready()) {
      return;
    }
    const std::uint64_t batch_epoch = pending.begin()->second.service_epoch;
    std::vector<std::pair<std::string, robot_simulation::LeaseToken>> sent_batch;
    std::vector<std::string> ids;
    std::vector<std::uint64_t> expected_revisions;
    for (const auto& [id, token] : pending) {
      if (token.service_epoch != batch_epoch) {
        continue;
      }
      sent_batch.emplace_back(id, token);
      ids.push_back(id);
      expected_revisions.push_back(token.revision);
    }
    auto req = std::make_shared<robot_interfaces_mission::srv::ReleaseRouteSegment::Request>();
    req->robot_id = robot_id_;
    req->resource_ids = std::move(ids);
    if (batch_epoch != 0U) {
      req->expected_service_epoch = batch_epoch;
      req->expected_revisions = std::move(expected_revisions);
    }
    release_call_.Send(
        release_client_, req,
        [this, sent_batch = std::move(sent_batch)](const auto& response) {
          if (!response->success) {
            return;
          }
          lease_tracker_.AcknowledgeReleaseBatch(sent_batch);
          if (!lease_tracker_.pending_releases().empty()) {
            FlushPendingReleases(Now());
          }
        },
        now_s);
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
    for (const auto& id : lease_tracker_.held()) {
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
      case Goal::kRelocate:
        if (current_station_ != relocation_destination_) {
          goal_station_ = relocation_destination_;
          phase_ = Phase::kRoute;
          return;
        }
        relocation_destination_.clear();
        [[fallthrough]];
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
  struct TaskStateUpdate {
    std::string task_id;
    std::string state;
    std::string message;
  };

  void UpdateTaskState(const std::string& state, const std::string& message) {
    if (task_id_.empty()) {
      return;
    }
    pending_task_updates_.push_back(TaskStateUpdate{task_id_, state, message});
    FlushPendingWrites(Now());
  }

  void UpdateFleetState(const std::string& state, const std::string& station) {
    pending_fleet_state_ = state;
    pending_fleet_station_ = station;
    fleet_state_dirty_ = true;
    ++fleet_state_seq_;
    FlushPendingWrites(Now());
  }

  void FlushPendingWrites(const double now_s) {
    if (fleet_state_dirty_ && !fleet_state_call_.pending() && fleet_state_client_->service_is_ready()) {
      const std::uint64_t sent_seq = fleet_state_seq_;
      auto req = std::make_shared<robot_interfaces_fleet::srv::UpdateFleetRobotState::Request>();
      req->robot_id = robot_id_;
      req->enabled = true;
      req->state = pending_fleet_state_;
      req->battery_voltage = 24.0;
      req->current_station_id = pending_fleet_station_;
      req->update_enabled = true;
      req->update_state = true;
      req->update_battery_voltage = true;
      req->update_current_station_id = true;
      fleet_state_call_.Send(
          fleet_state_client_, req,
          [this, sent_seq](const auto& response) {
            if (response->success && fleet_state_seq_ == sent_seq) {
              fleet_state_dirty_ = false;
            } else if (fleet_state_dirty_) {
              FlushPendingWrites(Now());
            }
          },
          now_s);
    }
    if (!pending_task_updates_.empty() && !task_state_call_.pending() &&
        task_state_client_->service_is_ready()) {
      const auto head = pending_task_updates_.front();
      auto req = std::make_shared<robot_interfaces_fleet::srv::UpdateRemoteTaskState::Request>();
      req->task_id = head.task_id;
      req->state = head.state;
      req->message = head.message;
      task_state_call_.Send(
          task_state_client_, req,
          [this](const auto& response) {
            if (response->success && !pending_task_updates_.empty()) {
              pending_task_updates_.pop_front();
              if (!pending_task_updates_.empty()) {
                FlushPendingWrites(Now());
              }
            }
          },
          now_s);
    }
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

  bool fleet_stopped_ = true;
  double fleet_stop_seen_s_ = 0.0;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr fleet_stop_sub_;
  Phase phase_ = Phase::kLoadMap;
  Goal goal_ = Goal::kNone;
  std::map<std::string, StationPose> stations_;
  std::map<std::string, std::vector<DirectedEdge>> graph_;
  std::set<EdgeKey> blocked_edges_;
  // M3：本车持有的资源与 v4 活跃纪元二元租约状态机；改道时临时绕开的路段与路口；退避目标。
  robot_simulation::AgentLeaseTracker lease_tracker_;
  std::deque<TaskStateUpdate> pending_task_updates_;
  std::string pending_fleet_state_;
  std::string pending_fleet_station_;
  bool fleet_state_dirty_ = false;
  std::uint64_t fleet_state_seq_ = 0;
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
  bool remote_control_enabled_ = false;
  bool restart_recovery_required_ = false;
  bool checkpoint_enabled_ = false;
  robot_simulation::ParkedTaskCheckpointStore parked_store_;
  std::optional<robot_simulation::ParkedTaskCheckpoint> parked_checkpoint_;
  std::string observed_traffic_mode_;
  std::string observed_traffic_authority_;
  std::optional<double> traffic_seen_s_;
  std::set<std::string> observed_owned_;
  bool canceled_hold_ = false;
  std::string canceled_task_id_;
  std::string relocation_destination_;
  bool stop_confirmed_ = false;
  std::mt19937_64 execution_rng_{std::random_device{}()};
  std::uint64_t task_execution_id_ = 0;
  std::uint64_t last_control_revision_ = 0;
  std::optional<std::uint64_t> active_stop_revision_;
  std::optional<double> dwell_frozen_at_;
  robot_simulation::TaskStopObservation stop_observation_;
  std::map<std::pair<std::uint64_t, std::uint64_t>, TaskControlReceipt> control_receipts_;
  rclcpp::Service<RemoteControl>::SharedPtr control_service_;
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
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_mission::srv::ListStations>>
      stations_call_;
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_mission::srv::ListStationRoutes>>
      routes_call_;
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_fleet::srv::ListRemoteTasks>>
      tasks_call_;
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_mission::srv::RequestRouteSegment>>
      route_call_;
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_mission::srv::ReleaseRouteSegment>>
      release_call_;
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_fleet::srv::UpdateFleetRobotState>>
      fleet_state_call_;
  robot_simulation::RetryingRequest<
      rclcpp::Client<robot_interfaces_fleet::srv::UpdateRemoteTaskState>>
      task_state_call_;
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
