#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

LOG_DIR="$(mktemp -d /tmp/robot_docking_state_machine.XXXXXX)"
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

start_bg navigate_sequence_server ros2 run robot_tasks navigate_sequence_server_node --ros-args \
  -p use_nav2_action:=false \
  -p simulate_without_nav2:=true
start_bg mission_runner ros2 launch robot_tasks mission_runner.launch.py \
  autostart:=false \
  server_timeout_ms:=3000 \
  return_to_dock_on_low_battery:=false

wait_service_type /v2/list_docks "robot_interfaces_mission/srv/ListDocks" 20 "${LOG_DIR}/wait_list_docks.err"
wait_service_type /v2/request_docking "robot_interfaces_mission/srv/RequestDocking" 20 "${LOG_DIR}/wait_request_docking.err"
wait_service_type /v2/request_undocking "robot_interfaces_mission/srv/RequestUndocking" 20 "${LOG_DIR}/wait_request_undocking.err"
wait_service_type /v2/get_dock_state "robot_interfaces_mission/srv/GetDockState" 20 "${LOG_DIR}/wait_get_dock_state.err"
wait_service_type /v2/list_facility_resources "robot_interfaces_facility/srv/ListFacilityResources" 20 "${LOG_DIR}/wait_facilities.err"
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 20 "${LOG_DIR}/wait_events.err"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_docks robot_interfaces_mission/srv/ListDocks "{include_disabled: false}")"
rg "success=True" <<<"${_rg_out}"
rg "dock_ids=\\['main_dock'\\]" <<<"${_rg_out}"
rg "states=\\['AVAILABLE'\\]" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_docking robot_interfaces_mission/srv/RequestDocking \
  "{dock_id: main_dock, request_id: alpha, priority: 50, start_if_idle: true, preempt_current: false, simulate_contact_success: true}")"
rg "success=True" <<<"${_rg_out}"
rg "dock_id='main_dock'" <<<"${_rg_out}"
rg "mission_id='docking_request_alpha'" <<<"${_rg_out}"
rg "state='APPROACHING'" <<<"${_rg_out}"

deadline=$((SECONDS + 40))
while true; do
  state_output="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_dock_state robot_interfaces_mission/srv/GetDockState "{dock_id: main_dock}" \
      2>"${LOG_DIR}/get_dock_state.err" || true
  )"
  if grep -q "success=True" <<<"${state_output}" && grep -q "state='CHARGING'" <<<"${state_output}"; then
    break
  fi
  if (( SECONDS >= deadline )); then
    echo "dock did not reach CHARGING state: ${state_output}" >&2
    exit 1
  fi
  sleep 1
done

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_facility_resources robot_interfaces_facility/srv/ListFacilityResources \
  "{resource_type: charger, include_disabled: false}")"
rg "success=True" <<<"${_rg_out}"
rg "resource_ids=\\['charger_main'\\]" <<<"${_rg_out}"
rg "status=\\['CHARGING'\\]" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_docking robot_interfaces_mission/srv/RequestDocking \
  "{dock_id: main_dock, request_id: beta, priority: 50, start_if_idle: false, preempt_current: false, simulate_contact_success: true}")"
rg "success=False" <<<"${_rg_out}"
rg "dock unavailable" <<<"${_rg_out}"
rg "state='CHARGING'" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
  "{limit: 20, state_filter: CHARGING, mission_id_filter: docking_request_alpha}")"
rg "success=True" <<<"${_rg_out}"
rg "docking_request_alpha" <<<"${_rg_out}"
rg "CHARGING" <<<"${_rg_out}"
rg "docked and charging" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_undocking robot_interfaces_mission/srv/RequestUndocking \
  "{dock_id: main_dock, request_id: alpha, release_charger: true}")"
rg "success=True" <<<"${_rg_out}"
rg "dock_id='main_dock'" <<<"${_rg_out}"
rg "state='UNDOCKED'" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_dock_state robot_interfaces_mission/srv/GetDockState "{dock_id: main_dock}")"
rg "success=True" <<<"${_rg_out}"
rg "state='UNDOCKED'" <<<"${_rg_out}"
rg "available=True" <<<"${_rg_out}"

echo "docking state machine passed"
