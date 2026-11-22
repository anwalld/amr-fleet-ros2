#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=robot_wait.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/robot_wait.sh"

ros2 service type /v2/list_map_zones | rg "robot_interfaces_navigation/srv/ListMapZones"
ros2 service type /v2/evaluate_map_position | rg "robot_interfaces_navigation/srv/EvaluateMapPosition"
ros2 topic type /map_manager/zones | rg "std_msgs/msg/String"
zones="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_map_zones robot_interfaces_navigation/srv/ListMapZones "{map_name: indoor_room, include_disabled: false}")"
rg "dock_slow_zone" <<<"${zones}"
rg "charging_keepout" <<<"${zones}"
service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/evaluate_map_position robot_interfaces_navigation/srv/EvaluateMapPosition "{map_name: indoor_room, point: {x: -1.0, y: -1.0, z: 0.0}}" | rg "speed_limit_mps=0.18|speed_limit_mps: 0.18"
service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/evaluate_map_position robot_interfaces_navigation/srv/EvaluateMapPosition "{map_name: indoor_room, point: {x: -2.0, y: 1.8, z: 0.0}}" | rg "allowed=False|allowed: false"
