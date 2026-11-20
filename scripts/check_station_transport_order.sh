#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/list_stations "robot_interfaces_mission/srv/ListStations" 10 /tmp/robot_station_list_service.err
wait_submit_order 10 /tmp/robot_station_order_service.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_station_order_topic.err

# 站点运输要等装卸确认，后台自动发（见 robot_wait.sh）。
start_station_auto_confirm

timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_stations robot_interfaces_mission/srv/ListStations "{}" \
  | rg "success=True" \
  | cat >/dev/null
_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_stations robot_interfaces_mission/srv/ListStations "{}" | response_only)"
rg "receiving" <<<"${_rg_out}"
rg "storage_a" <<<"${_rg_out}"

_rg_out="$(call_submit_order station_alpha station_transport 30 \
  "pickup_station=receiving;dropoff_station=storage_a;start_if_idle=true;preempt_current=false" \
  station_transport_order | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_order_station_alpha" <<<"${_rg_out}"

seen_order=false
deadline=$((SECONDS + 90))
while true; do
  active_goal="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState \
      --field active_goal_id 2>/tmp/robot_station_order_state.err || true
  )"
  state="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState \
      --field state 2>/tmp/robot_station_order_state.err || true
  )"

  if grep -q "station_order_station_alpha" <<<"${active_goal}"; then
    seen_order=true
  fi
  if ${seen_order} && grep -q "FINISHED" <<<"${state}"; then
    echo "station transport order mission finished"
    exit 0
  fi
  if grep -q "FAILED\\|ERROR" <<<"${state}"; then
    echo "station transport order failed with state: ${state}, active_goal_id: ${active_goal}" >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "station transport order did not finish; seen_order=${seen_order}, state=${state}, active_goal_id=${active_goal}" >&2
    exit 1
  fi
  sleep 1
done
