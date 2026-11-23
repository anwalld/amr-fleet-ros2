#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/update_fleet_robot_state "robot_interfaces_fleet/srv/UpdateFleetRobotState" 10 /tmp/robot_remote_fleet_update_robot.err
wait_submit_order 10 /tmp/robot_remote_fleet_submit.err
wait_service_type /v2/update_remote_task_state "robot_interfaces_fleet/srv/UpdateRemoteTaskState" 10 /tmp/robot_remote_fleet_update_task.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_remote_fleet_events.err
wait_service_type /v2/get_operator_snapshot "robot_interfaces_business/srv/GetOperatorSnapshot" 10 /tmp/robot_remote_fleet_snapshot.err

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/update_fleet_robot_state robot_interfaces_fleet/srv/UpdateFleetRobotState \
  "{robot_id: robot_1, enabled: true, state: busy, battery_voltage: 24.0, current_station_id: dock, update_enabled: true, update_state: true, update_battery_voltage: true, update_current_station_id: true}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "robot_1" <<<"${_rg_out}"
rg "busy" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/update_fleet_robot_state robot_interfaces_fleet/srv/UpdateFleetRobotState \
  "{robot_id: robot_2, enabled: true, state: available, battery_voltage: 24.2, current_station_id: receiving, update_enabled: true, update_state: true, update_battery_voltage: true, update_current_station_id: true}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "robot_2" <<<"${_rg_out}"
rg "available" <<<"${_rg_out}"

_rg_out="$(call_submit_order remote_alpha fleet_station 20 \
  "task_id=remote_alpha;required_capability=station_transport;pickup_station=receiving;dropoff_station=storage_a;start_if_idle=true;preempt_current=false" \
  fleet_station_task | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "remote_fleet_task_remote_alpha" <<<"${_rg_out}"
rg "dispatched remote fleet task" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
  "{limit: 20, state_filter: DISPATCHED_REMOTE, mission_id_filter: remote_fleet_task_remote_alpha}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "DISPATCHED_REMOTE" <<<"${_rg_out}"
rg "remote_fleet_task_remote_alpha" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/update_remote_task_state robot_interfaces_fleet/srv/UpdateRemoteTaskState \
  "{task_id: remote_alpha, state: FINISHED, message: remote robot completed task}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "remote_alpha" <<<"${_rg_out}"
rg "robot_2" <<<"${_rg_out}"
rg "FINISHED" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
  "{limit: 20, state_filter: FINISHED, mission_id_filter: remote_fleet_task_remote_alpha}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "FINISHED" <<<"${_rg_out}"
rg "remote robot completed task" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_operator_snapshot robot_interfaces_business/srv/GetOperatorSnapshot "{event_limit: 10}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "remote_task_ids=\\['remote_alpha'\\]" <<<"${_rg_out}"
rg "remote_task_robot_ids=\\['robot_2'\\]" <<<"${_rg_out}"
rg "remote_task_states=\\['FINISHED'\\]" <<<"${_rg_out}"

echo "remote fleet dispatch passed"
