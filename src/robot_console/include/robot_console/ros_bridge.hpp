#pragma once
// RosBridge：连接真实 ROS 系统的数据源。
// ROS 执行器跑在独立的 QThread 里；回调把消息转成结构体后直接 emit，接收方在界面线程，Qt 自动排队投递。
// 订阅：/traffic/state、/fleet/agent_events、每车 /<id>/odom（按快照动态订阅，可用参数覆盖话题）。
// 轮询：/v2/get_operator_snapshot 每秒一次；/v2/list_stations + /v2/list_station_routes 启动时一次、
// 之后每 5 秒一次，有变化才重发路网。每类请求同时最多挂一个，3 秒超时。
// ROS 头文件放在 Qt 之前，避免 Qt 的 signals/emit 宏影响 ROS 头文件。
#include <rclcpp/rclcpp.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "robot_console/request_tracker.hpp"
#include "robot_console/ros_operations.hpp"
#include "robot_console/ros_conversions.hpp"

#include <robot_interfaces/msg/robot_state.hpp>
#include <std_msgs/msg/bool.hpp>

#include <QThread>
#include "robot_console_core/data_source.hpp"

namespace robot_console {

class RosBridge : public DataSource {
  Q_OBJECT

 public:
  explicit RosBridge(QObject* parent = nullptr);
  ~RosBridge() override;

  QString description() const override { return QStringLiteral("ROS"); }
  void start() override;
  void stop() override;
  OperationSource* operations() { return &operations_; }

  // 解析 "robot_1=/odom" 形式的覆盖项。
  static std::map<std::string, std::string> parseOdomOverrides(const std::vector<std::string>& items);

 private:
  using GetSnapshot = robot_interfaces_business::srv::GetOperatorSnapshot;
  using ListStations = robot_interfaces_mission::srv::ListStations;
  using ListRoutes = robot_interfaces_mission::srv::ListStationRoutes;

  double nowS() const;
  void pollSnapshot();
  void pollGraph();
  void checkTimeouts();
  void onSnapshot(const GetSnapshot::Response& res);
  void maybeEmitGraph();
  void ensureOdomSubscriptions(const std::vector<std::string>& robot_ids);
  void setConnected(bool connected, const QString& detail);
  void snapshotUnavailable(const QString& detail);

  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  QThread* thread_ = nullptr;

  rclcpp::Client<GetSnapshot>::SharedPtr snapshot_client_;
  rclcpp::Client<ListStations>::SharedPtr stations_client_;
  rclcpp::Client<ListRoutes>::SharedPtr routes_client_;
  rclcpp::Subscription<robot_interfaces_mission::msg::TrafficState>::SharedPtr traffic_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr events_sub_;
  rclcpp::Subscription<robot_interfaces::msg::RobotState>::SharedPtr supervisor_sub_;
  rclcpp::Subscription<std_msgs::msg::Bool>::SharedPtr fleet_stop_sub_;
  std::map<std::string, rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr> odom_subs_;
  std::map<std::string, std::string> odom_overrides_;
  RosOperations operations_;
  rclcpp::TimerBase::SharedPtr first_graph_timer_;
  rclcpp::TimerBase::SharedPtr snapshot_timer_;
  rclcpp::TimerBase::SharedPtr graph_timer_;
  rclcpp::TimerBase::SharedPtr timeout_timer_;

  RequestTracker snapshot_tracker_;
  RequestTracker stations_tracker_;
  RequestTracker routes_tracker_;
  std::optional<ListStations::Response> pending_stations_;
  std::optional<ListRoutes::Response> pending_routes_;
  std::optional<StationGraph> last_graph_;
  bool connected_ = false;
  bool snapshot_available_ = false;
  bool recovering_graph_ = false;
  QString connection_detail_;
  bool connection_known_ = false;
};

}  // namespace robot_console
