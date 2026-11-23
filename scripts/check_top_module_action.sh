#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

LOG_DIR="$(mktemp -d /tmp/robot_top_module.XXXXXX)"
PIDS=()

cleanup() {
  for pid in "${PIDS[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
  wait "${PIDS[@]:-}" >/dev/null 2>&1 || true
  rm -rf "${LOG_DIR}"
}
trap cleanup EXIT

start_bg() {
  local name="$1"
  shift
  "$@" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_bg mission_runner ros2 launch robot_tasks mission_runner.launch.py \
  autostart:=false \
  return_to_dock_on_low_battery:=false \
  payload_capacity_kg:=40.0 \
  top_module_type:=lift

wait_service_type /v2/set_payload_loaded "robot_interfaces_facility/srv/SetPayloadLoaded" 20 \
  "${LOG_DIR}/wait_set_payload.err"
wait_service_type /v2/execute_top_module_action "robot_interfaces_facility/srv/ExecuteTopModuleAction" 20 \
  "${LOG_DIR}/wait_top_module.err"
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 20 \
  "${LOG_DIR}/wait_events.err"
wait_topic_type /payload/state "robot_interfaces/msg/PayloadState" 20 \
  "${LOG_DIR}/wait_payload_state.err"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/set_payload_loaded robot_interfaces_facility/srv/SetPayloadLoaded \
  "{loaded: true, payload_id: shelf_alpha, weight_kg: 18.0, message: shelf loaded for lift test}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "payload_id='shelf_alpha'" <<<"${_rg_out}"
rg "state='LOADED'" <<<"${_rg_out}"
rg "loaded=True" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/execute_top_module_action robot_interfaces_facility/srv/ExecuteTopModuleAction \
  "{action: lift_up, payload_id: shelf_alpha, weight_kg: 18.0}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "action='lift_up'" <<<"${_rg_out}"
rg "state='TOP_MODULE_ACTION'" <<<"${_rg_out}"
rg "loaded=True" <<<"${_rg_out}"
rg "payload_id='shelf_alpha'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/execute_top_module_action robot_interfaces_facility/srv/ExecuteTopModuleAction \
  "{action: attach, payload_id: shelf_alpha, weight_kg: 18.0}" | response_only)"
rg "success=False" <<<"${_rg_out}"
rg "unsupported top module action attach for lift" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /payload/state robot_interfaces/msg/PayloadState)"
rg "loaded: true" <<<"${_rg_out}"
rg "payload_id: shelf_alpha" <<<"${_rg_out}"
rg "state: TOP_MODULE_ACTION" <<<"${_rg_out}"
rg "last_action: lift_up" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
  "{limit: 20, state_filter: TOP_MODULE_ACTION, mission_id_filter: top_module_lift_up}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "TOP_MODULE_ACTION" <<<"${_rg_out}"
rg "executed lift_up on lift" <<<"${_rg_out}"

echo "top module action passed"
