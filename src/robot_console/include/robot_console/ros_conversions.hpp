#pragma once
// ROS 消息到 robot_console_core 结构体的转换。字段逐个照抄，与回放文件（JSONL）的解析一一对应。
#include <nav_msgs/msg/odometry.hpp>
#include <robot_interfaces_business/srv/get_operator_snapshot.hpp>
#include <robot_interfaces_mission/msg/traffic_state.hpp>
#include <robot_interfaces_mission/srv/list_station_routes.hpp>
#include <robot_interfaces_mission/srv/list_stations.hpp>
#include <std_msgs/msg/string.hpp>

#include "robot_console_core/types.hpp"

namespace robot_console {

TrafficStateData trafficFromRos(const robot_interfaces_mission::msg::TrafficState& msg);
OperatorSnapshotData snapshotFromRos(const robot_interfaces_business::srv::GetOperatorSnapshot::Response& res);
StationGraph graphFromRos(const robot_interfaces_mission::srv::ListStations::Response& stations,
                          const robot_interfaces_mission::srv::ListStationRoutes::Response& routes);
PoseSample poseFromOdom(const QString& robot, const nav_msgs::msg::Odometry& odom);
FleetEvent eventFromRos(const std_msgs::msg::String& msg);

double yawFromQuaternion(double x, double y, double z, double w);

}  // namespace robot_console
