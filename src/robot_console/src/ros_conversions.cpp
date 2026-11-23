#include "robot_console/ros_conversions.hpp"

#include <QJsonDocument>

#include <cmath>

namespace robot_console {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QStringList qs(const std::vector<std::string>& v) {
  QStringList out;
  out.reserve(static_cast<int>(v.size()));
  for (const auto& s : v) out.push_back(q(s));
  return out;
}

template <typename T, typename U>
QVector<T> qv(const std::vector<U>& v) {
  QVector<T> out;
  out.reserve(static_cast<int>(v.size()));
  for (const auto& x : v) out.push_back(static_cast<T>(x));
  return out;
}

double seconds(const builtin_interfaces::msg::Time& t) {
  return static_cast<double>(t.sec) + static_cast<double>(t.nanosec) * 1e-9;
}

}  // namespace

double yawFromQuaternion(double x, double y, double z, double w) {
  return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
}

TrafficStateData trafficFromRos(const robot_interfaces_mission::msg::TrafficState& m) {
  TrafficStateData s;
  s.stamp = seconds(m.stamp);
  s.mode = q(m.mode);
  s.resource_ids = qs(m.resource_ids);
  s.owner_ids = qs(m.owner_ids);
  s.waiting_resource_ids = qs(m.waiting_resource_ids);
  s.waiting_robot_ids = qs(m.waiting_robot_ids);
  s.waiting_priorities = qv<int>(m.waiting_priorities);
  s.waiting_positions = qv<int>(m.waiting_positions);
  s.grant_count = m.grant_count;
  s.queue_count = m.queue_count;
  s.deadlock_check_count = m.deadlock_check_count;
  s.deadlock_count = m.deadlock_count;
  s.deadlock_robot_ids = qs(m.deadlock_robot_ids);
  s.last_resolution_action = q(m.last_resolution_action);
  s.last_resolution_robot_id = q(m.last_resolution_robot_id);
  s.last_resolution_ms = m.last_resolution_ms;
  s.resolved_count = m.resolved_count;
  s.last_detection_us = m.last_detection_us;
  s.max_detection_us = m.max_detection_us;
  s.reroute_count = m.reroute_count;
  s.backoff_count = m.backoff_count;
  s.pause_count = m.pause_count;
  return s;
}

OperatorSnapshotData snapshotFromRos(const robot_interfaces_business::srv::GetOperatorSnapshot::Response& r) {
  OperatorSnapshotData s;
  s.success = r.success;
  s.message = q(r.message);
  s.mission_state = q(r.mission_state);
  s.previous_mission_state = q(r.previous_mission_state);
  s.active_mission_id = q(r.active_mission_id);
  s.mission_message = q(r.mission_message);
  s.recoverable = r.recoverable;
  s.mission_active = r.mission_active;
  s.paused = r.paused;
  s.queue_size = r.queue_size;
  s.queued_mission_ids = qs(r.queued_mission_ids);
  s.queued_priorities = qv<int>(r.queued_priorities);
  s.paused_workflow_order_ids = qs(r.paused_workflow_order_ids);
  s.fleet_robot_ids = qs(r.fleet_robot_ids);
  s.fleet_enabled = qv<bool>(r.fleet_enabled);
  s.fleet_states = qs(r.fleet_states);
  s.fleet_battery_voltage = qv<double>(r.fleet_battery_voltage);
  s.fleet_current_station_ids = qs(r.fleet_current_station_ids);
  s.fleet_capabilities = qs(r.fleet_capabilities);
  s.remote_task_ids = qs(r.remote_task_ids);
  s.remote_task_robot_ids = qs(r.remote_task_robot_ids);
  s.remote_task_states = qs(r.remote_task_states);
  s.facility_resource_ids = qs(r.facility_resource_ids);
  s.facility_resource_types = qs(r.facility_resource_types);
  s.facility_station_ids = qs(r.facility_station_ids);
  s.facility_available = qv<bool>(r.facility_available);
  s.facility_status = qs(r.facility_status);
  s.business_types = qs(r.business_types);
  s.business_scenario_ids = qs(r.business_scenario_ids);
  s.business_workflow_ids = qs(r.business_workflow_ids);
  s.recent_event_stamp = qs(r.recent_event_stamp);
  s.recent_event_mission_ids = qs(r.recent_event_mission_ids);
  s.recent_event_states = qs(r.recent_event_states);
  s.recent_event_messages = qs(r.recent_event_messages);
  return s;
}

StationGraph graphFromRos(const robot_interfaces_mission::srv::ListStations::Response& st,
                          const robot_interfaces_mission::srv::ListStationRoutes::Response& rt) {
  StationGraph g;
  g.station_ids = qs(st.station_ids);
  g.frame_ids = qs(st.frame_ids);
  g.x = qv<double>(st.x);
  g.y = qv<double>(st.y);
  g.yaw = qv<double>(st.yaw);
  g.from_station = qs(rt.from_station);
  g.to_station = qs(rt.to_station);
  g.distance_m = qv<double>(rt.distance_m);
  g.bidirectional = qv<bool>(rt.bidirectional);
  g.enabled = qv<bool>(rt.enabled);
  return g;
}

PoseSample poseFromOdom(const QString& robot, const nav_msgs::msg::Odometry& o) {
  PoseSample p;
  p.robot = robot;
  p.stamp = seconds(o.header.stamp);
  p.x = o.pose.pose.position.x;
  p.y = o.pose.pose.position.y;
  const auto& qn = o.pose.pose.orientation;
  p.yaw = yawFromQuaternion(qn.x, qn.y, qn.z, qn.w);
  p.vx = o.twist.twist.linear.x;
  p.wz = o.twist.twist.angular.z;
  return p;
}

FleetEvent eventFromRos(const std_msgs::msg::String& msg) {
  FleetEvent e;
  const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(msg.data));
  if (doc.isObject()) {
    e.data = doc.object();
  } else {
    e.data.insert(QStringLiteral("raw"), q(msg.data));
  }
  return e;
}

}  // namespace robot_console
