#include <gtest/gtest.h>

#include <cmath>

#include "robot_console/ros_bridge.hpp"
#include "robot_console/ros_conversions.hpp"

using namespace robot_console;

TEST(RosConversions, Traffic) {
  robot_interfaces_mission::msg::TrafficState m;
  m.stamp.sec = 1795000001;
  m.stamp.nanosec = 500000000;
  m.mode = "rolling";
  m.resource_ids = {"route_edge:a__b", "route_node:c"};
  m.owner_ids = {"robot_2", "robot_1"};
  m.waiting_resource_ids = {"route_edge:a__b"};
  m.waiting_robot_ids = {"robot_1"};
  m.waiting_priorities = {3};
  m.waiting_positions = {0};
  m.grant_count = 12;
  m.deadlock_robot_ids = {"robot_1", "robot_2"};
  m.last_resolution_action = "backoff";
  m.last_resolution_robot_id = "robot_2";
  m.last_resolution_ms = -1.0;
  m.resolved_count = 4;
  m.max_detection_us = 21.5;
  m.pause_count = 1;
  const TrafficStateData s = trafficFromRos(m);
  EXPECT_DOUBLE_EQ(s.stamp, 1795000001.5);
  EXPECT_EQ(s.mode, QStringLiteral("rolling"));
  EXPECT_EQ(s.resource_ids, (QStringList{"route_edge:a__b", "route_node:c"}));
  EXPECT_EQ(s.owner_ids.value(1), QStringLiteral("robot_1"));
  EXPECT_EQ(s.waiting_priorities, QVector<int>{3});
  EXPECT_EQ(s.grant_count, 12u);
  EXPECT_EQ(s.deadlock_robot_ids.size(), 2);
  EXPECT_EQ(s.last_resolution_action, QStringLiteral("backoff"));
  EXPECT_DOUBLE_EQ(s.last_resolution_ms, -1.0);
  EXPECT_EQ(s.resolved_count, 4u);
  EXPECT_DOUBLE_EQ(s.max_detection_us, 21.5);
  EXPECT_EQ(s.pause_count, 1u);
}

TEST(RosConversions, Snapshot) {
  robot_interfaces_business::srv::GetOperatorSnapshot::Response r;
  r.success = true;
  r.mission_state = "RUNNING";
  r.active_mission_id = "m9";
  r.queue_size = 2;
  r.queued_mission_ids = {"m3", "m1"};
  r.queued_priorities = {5, 1};
  r.fleet_robot_ids = {"robot_1", "robot_2"};
  r.fleet_enabled = {true, false};
  r.fleet_states = {"available", "busy"};
  r.fleet_battery_voltage = {24.1, 23.85};
  r.remote_task_ids = {"t1"};
  r.remote_task_robot_ids = {"robot_2"};
  r.remote_task_states = {"RUNNING"};
  const OperatorSnapshotData s = snapshotFromRos(r);
  EXPECT_TRUE(s.success);
  EXPECT_EQ(s.mission_state, QStringLiteral("RUNNING"));
  EXPECT_EQ(s.active_mission_id, QStringLiteral("m9"));
  EXPECT_EQ(s.queue_size, 2);
  EXPECT_EQ(s.queued_priorities, (QVector<int>{5, 1}));
  EXPECT_EQ(s.fleet_enabled, (QVector<bool>{true, false}));
  EXPECT_DOUBLE_EQ(s.fleet_battery_voltage.value(1), 23.85);
  EXPECT_EQ(s.remote_task_robot_ids, QStringList{"robot_2"});
}

TEST(RosConversions, Graph) {
  robot_interfaces_mission::srv::ListStations::Response st;
  st.success = true;
  st.station_ids = {"a", "b"};
  st.frame_ids = {"map", "map"};
  st.x = {0.0, 4.0};
  st.y = {0.0, 0.0};
  st.yaw = {0.0, 1.57};
  robot_interfaces_mission::srv::ListStationRoutes::Response rt;
  rt.success = true;
  rt.from_station = {"a"};
  rt.to_station = {"b"};
  rt.distance_m = {4.0};
  rt.bidirectional = {true};
  rt.enabled = {false};
  const StationGraph g = graphFromRos(st, rt);
  EXPECT_EQ(g.station_ids, (QStringList{"a", "b"}));
  EXPECT_DOUBLE_EQ(g.x.value(1), 4.0);
  EXPECT_DOUBLE_EQ(g.yaw.value(1), 1.57);
  EXPECT_EQ(g.to_station, QStringList{"b"});
  EXPECT_EQ(g.bidirectional, QVector<bool>{true});
  EXPECT_EQ(g.enabled, QVector<bool>{false});
  StationGraph g2 = g;
  EXPECT_TRUE(g == g2);
  g2.enabled[0] = true;  // 封路状态变化：路网不同，需要重发
  EXPECT_TRUE(g != g2);
}

TEST(RosConversions, PoseFromOdom) {
  nav_msgs::msg::Odometry o;
  o.header.stamp.sec = 100;
  o.header.stamp.nanosec = 250000000;
  o.pose.pose.position.x = 2.0;
  o.pose.pose.position.y = 1.0;
  // yaw = 90°
  o.pose.pose.orientation.z = std::sin(M_PI / 4);
  o.pose.pose.orientation.w = std::cos(M_PI / 4);
  o.twist.twist.linear.x = 0.5;
  o.twist.twist.angular.z = 0.1;
  const PoseSample p = poseFromOdom(QStringLiteral("robot_3"), o);
  EXPECT_EQ(p.robot, QStringLiteral("robot_3"));
  EXPECT_DOUBLE_EQ(p.stamp, 100.25);
  EXPECT_DOUBLE_EQ(p.x, 2.0);
  EXPECT_NEAR(p.yaw, M_PI / 2, 1e-9);
  EXPECT_DOUBLE_EQ(p.vx, 0.5);
  EXPECT_DOUBLE_EQ(p.wz, 0.1);
}

TEST(RosConversions, Event) {
  std_msgs::msg::String m;
  m.data = R"({"robot":"robot_2","task":"t1","duration_s":8.0})";
  const FleetEvent e = eventFromRos(m);
  EXPECT_EQ(e.data.value(QStringLiteral("task")).toString(), QStringLiteral("t1"));
  EXPECT_DOUBLE_EQ(e.data.value(QStringLiteral("duration_s")).toDouble(), 8.0);
  m.data = "not json";
  EXPECT_EQ(eventFromRos(m).data.value(QStringLiteral("raw")).toString(), QStringLiteral("not json"));
}

TEST(RosBridge, ParseOdomOverrides) {
  const auto m = RosBridge::parseOdomOverrides({"robot_1=/odom", "bad", "=x", "robot_2="});
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m.at("robot_1"), "/odom");
}
