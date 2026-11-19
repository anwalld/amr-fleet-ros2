#pragma once
// 调度台的数据结构。字段名逐个照抄对应的 ROS 接口，回放文件（JSONL）也用同一套字段名。
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace robot_console {

// 回放文件第一行（kind=meta）。
struct ReplayMeta {
  int v = 0;
  QString network;
  int robots = 0;
  QString mode;
  QString rev;
  QString source;
  QJsonObject raw;  // 全部键，含将来新增的键
};

// /v2/list_stations + /v2/list_station_routes 的应答。
struct StationGraph {
  QStringList station_ids;
  QStringList frame_ids;
  QVector<double> x;
  QVector<double> y;
  QVector<double> yaw;
  QStringList from_station;
  QStringList to_station;
  QVector<double> distance_m;
  QVector<bool> bidirectional;
  QVector<bool> enabled;

  bool operator==(const StationGraph& o) const;
  bool operator!=(const StationGraph& o) const { return !(*this == o); }
};

// 车辆位置：里程计消息精简后的字段。
struct PoseSample {
  QString robot;
  double stamp = 0.0;  // 消息头时间戳（秒）
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
  double vx = 0.0;
  double wz = 0.0;
};

// /traffic/state（robot_interfaces_mission/msg/TrafficState）。
struct TrafficStateData {
  double stamp = 0.0;
  QString mode;
  QStringList resource_ids;
  QStringList owner_ids;
  QStringList waiting_resource_ids;
  QStringList waiting_robot_ids;
  QVector<int> waiting_priorities;
  QVector<int> waiting_positions;
  quint64 grant_count = 0;
  quint64 queue_count = 0;
  quint64 deadlock_check_count = 0;
  quint64 deadlock_count = 0;
  QStringList deadlock_robot_ids;
  QString last_resolution_action;
  QString last_resolution_robot_id;
  double last_resolution_ms = -1.0;
  quint64 resolved_count = 0;
  double last_detection_us = 0.0;
  double max_detection_us = 0.0;
  quint64 reroute_count = 0;
  quint64 backoff_count = 0;
  quint64 pause_count = 0;
};

// /v2/get_operator_snapshot 的应答（robot_interfaces_business/srv/GetOperatorSnapshot）。
struct OperatorSnapshotData {
  bool success = false;
  QString message;
  QString mission_state;
  QString previous_mission_state;
  QString active_mission_id;
  QString mission_message;
  bool recoverable = false;
  bool mission_active = false;
  bool paused = false;
  int queue_size = 0;
  QStringList queued_mission_ids;
  QVector<int> queued_priorities;
  QStringList paused_workflow_order_ids;
  QStringList fleet_robot_ids;
  QVector<bool> fleet_enabled;
  QStringList fleet_states;
  QVector<double> fleet_battery_voltage;
  QStringList fleet_current_station_ids;
  QStringList fleet_capabilities;
  QStringList remote_task_ids;
  QStringList remote_task_robot_ids;
  QStringList remote_task_states;
  QStringList facility_resource_ids;
  QStringList facility_resource_types;
  QStringList facility_station_ids;
  QVector<bool> facility_available;
  QStringList facility_status;
  QStringList business_types;
  QStringList business_scenario_ids;
  QStringList business_workflow_ids;
  QStringList recent_event_stamp;
  QStringList recent_event_mission_ids;
  QStringList recent_event_states;
  QStringList recent_event_messages;
};

// /fleet/agent_events：消息内容是 JSON，解析后的对象。
struct FleetEvent {
  QJsonObject data;
};

struct ConnectionStatus {
  bool connected = false;
  QString detail;
};

// 单调时钟（毫秒），测试时可注入。
using MonotonicMsFn = std::function<qint64()>;
MonotonicMsFn defaultMonotonicMs();
// 系统时间（秒，Unix 纪元），与 ROS 消息头时间戳同一时基。
double systemNowSeconds();

// 跨线程排队信号要用的元类型，启动时调用一次。
void registerMetaTypes();

}  // namespace robot_console

Q_DECLARE_METATYPE(robot_console::StationGraph)
Q_DECLARE_METATYPE(robot_console::PoseSample)
Q_DECLARE_METATYPE(robot_console::TrafficStateData)
Q_DECLARE_METATYPE(robot_console::OperatorSnapshotData)
Q_DECLARE_METATYPE(robot_console::FleetEvent)
Q_DECLARE_METATYPE(robot_console::ConnectionStatus)
