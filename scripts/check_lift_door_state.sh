#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

LOG_DIR="$(mktemp -d /tmp/robot_lift_door.XXXXXX)"
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
  return_to_dock_on_low_battery:=false

wait_service_type /v2/get_lift_state "robot_interfaces_facility/srv/GetLiftState" 20 \
  "${LOG_DIR}/wait_lift_state.err"
wait_service_type /v2/get_door_state "robot_interfaces_facility/srv/GetDoorState" 20 \
  "${LOG_DIR}/wait_door_state.err"
wait_service_type /v2/request_lift_session "robot_interfaces_facility/srv/RequestLiftSession" 20 \
  "${LOG_DIR}/wait_lift_request.err"
wait_service_type /v2/release_lift_session "robot_interfaces_facility/srv/ReleaseLiftSession" 20 \
  "${LOG_DIR}/wait_lift_release.err"
wait_service_type /v2/reserve_facility_resource "robot_interfaces_facility/srv/ReserveFacilityResource" 20 \
  "${LOG_DIR}/wait_reserve.err"
wait_service_type /v2/release_facility_resource "robot_interfaces_facility/srv/ReleaseFacilityResource" 20 \
  "${LOG_DIR}/wait_release.err"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_lift_state robot_interfaces_facility/srv/GetLiftState \
  "{lift_id: freight_elevator}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "lift_id='freight_elevator'" <<<"${_rg_out}"
rg "station_id='packing'" <<<"${_rg_out}"
rg "available=True" <<<"${_rg_out}"
rg "status='available'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/request_lift_session robot_interfaces_facility/srv/RequestLiftSession \
  "{session_id: lift_session_alpha, lift_id: freight_elevator, requester_id: robot_1, from_station: receiving, to_station: packing, release_existing_for_requester: true}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "session_id='lift_session_alpha'" <<<"${_rg_out}"
rg "lift_id='freight_elevator'" <<<"${_rg_out}"
rg "status='lift_session'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_lift_state robot_interfaces_facility/srv/GetLiftState \
  "{lift_id: freight_elevator}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "available=False" <<<"${_rg_out}"
rg "holder_id='robot_1'" <<<"${_rg_out}"
rg "status='lift_session'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/request_lift_session robot_interfaces_facility/srv/RequestLiftSession \
  "{session_id: lift_session_beta, lift_id: freight_elevator, requester_id: robot_2, from_station: receiving, to_station: packing, release_existing_for_requester: false}" | response_only)"
rg "success=False" <<<"${_rg_out}"
rg "lift already reserved by robot_1" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/release_lift_session robot_interfaces_facility/srv/ReleaseLiftSession \
  "{session_id: lift_session_alpha, lift_id: freight_elevator}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "status='available'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_lift_state robot_interfaces_facility/srv/GetLiftState \
  "{lift_id: freight_elevator}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "available=True" <<<"${_rg_out}"
rg "status='available'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_door_state robot_interfaces_facility/srv/GetDoorState \
  "{door_id: receiving_door}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "door_id='receiving_door'" <<<"${_rg_out}"
rg "station_id='receiving'" <<<"${_rg_out}"
rg "available=True" <<<"${_rg_out}"
rg "status='available'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/reserve_facility_resource robot_interfaces_facility/srv/ReserveFacilityResource \
  "{resource_id: receiving_door, holder_id: operator_a, mission_id: door_hold_alpha, release_existing_for_holder: true}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "status='reserved'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_door_state robot_interfaces_facility/srv/GetDoorState \
  "{door_id: receiving_door}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "available=False" <<<"${_rg_out}"
rg "holder_id='operator_a'" <<<"${_rg_out}"
rg "status='reserved'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/release_facility_resource robot_interfaces_facility/srv/ReleaseFacilityResource \
  "{resource_id: receiving_door, holder_id: operator_a}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "status='available'" <<<"${_rg_out}"

echo "lift and door state passed"
