#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_low_battery_dock_topic.err

# 连续发布低电量：单次 `pub --once` 常在订阅匹配前就退出，消息丢失（design D1 更正）。
timeout 15 ros2 topic pub --times 6 --rate 2 /chassis/state robot_interfaces/msg/ChassisState \
  "{backend: mock, kinematics_model: diff_drive, connected: true, battery_voltage: 21.0, linear_velocity: 0.0, angular_velocity: 0.0, status: low_battery_test}" \
  >/tmp/robot_low_battery_dock_pub.log

wait_active_mission dock_return 90
echo "low-battery return-to-dock mission finished"
