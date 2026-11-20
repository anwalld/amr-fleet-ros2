#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/estimate_station_sequence "robot_interfaces_mission/srv/EstimateStationSequence" 10 /tmp/robot_station_sequence_estimate.err
wait_submit_order 10 /tmp/robot_station_sequence_submit.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_station_sequence_events.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_station_sequence_state_topic.err

# 站点运输要等装卸确认，后台自动发（见 robot_wait.sh）。
start_station_auto_confirm

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/estimate_station_sequence robot_interfaces_mission/srv/EstimateStationSequence \
  "{station_ids: [receiving, storage_a, packing, dock], nominal_speed_mps: 0.5, battery_voltage: 24.5}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "estimate_station_sequence" <<<"${_rg_out}"
rg "waypoint_count=4" <<<"${_rg_out}"
# 原来写死的 3.9 是 warehouse 站点的距离，本用例跑在 amr_demo 上；改为与逐段估算之和一致。
seq_distance="$(resp_scalar distance_m <<<"${_rg_out}")"
leg_sum=0
for leg in "receiving storage_a" "storage_a packing" "packing dock"; do
  read -r from to <<<"${leg}"
  leg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/estimate_station_transport robot_interfaces_mission/srv/EstimateStationTransport \
    "{pickup_station: ${from}, dropoff_station: ${to}, nominal_speed_mps: 0.5, battery_voltage: 24.5}" | response_only)"
  leg_sum="$(awk -v a="${leg_sum}" -v b="$(resp_scalar distance_m <<<"${leg_out}")" 'BEGIN { print a + b }')"
done
expect_close "sequence distance = sum of legs" "${seq_distance}" "${leg_sum}" 0.05
rg "battery_sufficient=True" <<<"${_rg_out}"

_rg_out="$(call_submit_order sequence_alpha station_sequence 35 \
  "station_ids=receiving,storage_a,packing,dock;start_if_idle=true;preempt_current=false" \
  station_sequence | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_sequence_sequence_alpha_leg_1" <<<"${_rg_out}"
rg "station_sequence_sequence_alpha_leg_2" <<<"${_rg_out}"
rg "station_sequence_sequence_alpha_leg_3" <<<"${_rg_out}"

seen_first=false
seen_second=false
seen_third=false
deadline=$((SECONDS + 180))
while true; do
  state="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState \
      --field state 2>/tmp/robot_station_sequence_state.err || true
  )"

  if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
    "{limit: 50, state_filter: FINISHED, mission_id_filter: station_sequence_sequence_alpha_leg_1}" \
    2>/tmp/robot_station_sequence_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
    seen_first=true
  fi
  if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
    "{limit: 50, state_filter: FINISHED, mission_id_filter: station_sequence_sequence_alpha_leg_2}" \
    2>/tmp/robot_station_sequence_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
    seen_second=true
  fi
  if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
    "{limit: 50, state_filter: FINISHED, mission_id_filter: station_sequence_sequence_alpha_leg_3}" \
    2>/tmp/robot_station_sequence_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
    seen_third=true
  fi
  if ${seen_first} && ${seen_second} && ${seen_third} && grep -q "FINISHED" <<<"${state}"; then
    echo "station sequence task passed"
    exit 0
  fi
  if grep -q "FAILED\\|ERROR" <<<"${state}"; then
    echo "station sequence task failed with state: ${state}, active_goal_id: ${active_goal}" >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "station sequence task did not finish; seen_first=${seen_first}, seen_second=${seen_second}, seen_third=${seen_third}, state=${state}" >&2
    exit 1
  fi
  sleep 1
done
