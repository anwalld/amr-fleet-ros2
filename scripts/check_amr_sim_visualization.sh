#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 1))}"
: "${LIBGL_ALWAYS_SOFTWARE:=1}" "${MESA_LOADER_DRIVER_OVERRIDE:=kms_swrast}"
: "${GALLIUM_DRIVER:=llvmpipe}" "${MESA_GL_VERSION_OVERRIDE:=4.5}"
export LIBGL_ALWAYS_SOFTWARE MESA_LOADER_DRIVER_OVERRIDE GALLIUM_DRIVER MESA_GL_VERSION_OVERRIDE

LOG_DIR="$(mktemp -d /tmp/robot_amr_sim_visualization.XXXXXX)"
PIDS=()

cleanup() {
  for pgid in "${PIDS[@]:-}"; do
    kill -INT -- "-${pgid}" >/dev/null 2>&1 || true
  done
  sleep 1
  for pgid in "${PIDS[@]:-}"; do
    kill -TERM -- "-${pgid}" >/dev/null 2>&1 || true
  done
  sleep 1
  for pgid in "${PIDS[@]:-}"; do
    kill -KILL -- "-${pgid}" >/dev/null 2>&1 || true
  done
  wait "${PIDS[@]:-}" >/dev/null 2>&1 || true
  rm -rf "${LOG_DIR}"
}
trap cleanup EXIT

start_bg() {
  local name="$1"
  shift
  setsid "$@" >"${LOG_DIR}/${name}.log" 2>&1 < /dev/null &
  PIDS+=("$!")
}

wait_marker_text() {
  local pattern="$1"
  local timeout_sec="${2:-20}"
  local deadline=$((SECONDS + timeout_sec))
  local sample="${LOG_DIR}/markers.txt"
  while true; do
    if timeout 5s ros2 topic echo --once --full-length /amr_simulation/markers \
        visualization_msgs/msg/MarkerArray >"${sample}" 2>"${LOG_DIR}/markers.err" &&
      rg "${pattern}" "${sample}" >/dev/null; then
      return 0
    fi
    if ((SECONDS >= deadline)); then
      echo "marker text did not match pattern: ${pattern}" >&2
      cat "${sample}" >&2 2>/dev/null || true
      cat "${LOG_DIR}/markers.err" >&2 2>/dev/null || true
      return 1
    fi
    sleep 1
  done
}

read_odom_field() {
  local field="$1"
  timeout 5s ros2 topic echo --once /model/mobile_robot/odometry \
    nav_msgs/msg/Odometry --field "${field}" 2>"${LOG_DIR}/odom_${field//./_}.err" \
    | rg -m 1 '^-?[0-9]+([.][0-9]+)?([eE][-+]?[0-9]+)?$'
}

start_bg amr_demo ros2 launch robot_bringup amr_demo.launch.py gui:=false use_rviz:=false auto_demo:=false

wait_topic_type /amr_simulation/markers "visualization_msgs/msg/MarkerArray" 30 \
  "${LOG_DIR}/wait_markers.err"
wait_service_type /v2/request_docking "robot_interfaces_mission/srv/RequestDocking" 30 \
  "${LOG_DIR}/wait_docking.err"
wait_service_type /v2/get_dock_state "robot_interfaces_mission/srv/GetDockState" 30 \
  "${LOG_DIR}/wait_dock_state.err"
wait_service_type /v2/set_payload_loaded "robot_interfaces_facility/srv/SetPayloadLoaded" 30 \
  "${LOG_DIR}/wait_payload.err"
wait_service_type /v2/request_station_confirmation "robot_interfaces_facility/srv/RequestStationConfirmation" 30 \
  "${LOG_DIR}/wait_confirmation.err"
wait_service_type /v2/block_station_route "robot_interfaces_mission/srv/BlockStationRoute" 30 \
  "${LOG_DIR}/wait_block_route.err"
wait_service_type /v2/request_lift_session "robot_interfaces_facility/srv/RequestLiftSession" 30 \
  "${LOG_DIR}/wait_lift.err"
wait_service_type /v2/reserve_facility_resource "robot_interfaces_facility/srv/ReserveFacilityResource" 30 \
  "${LOG_DIR}/wait_facility.err"
wait_service_type /v2/set_dynamic_speed_limit "robot_interfaces_navigation/srv/SetDynamicSpeedLimit" 30 \
  "${LOG_DIR}/wait_speed.err"
wait_service_type /v2/report_obstacle_blockage "robot_interfaces_navigation/srv/ReportObstacleBlockage" 30 \
  "${LOG_DIR}/wait_obstacle.err"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/request_docking robot_interfaces_mission/srv/RequestDocking \
  "{dock_id: main_dock, request_id: sim_visual, priority: 50, start_if_idle: true, preempt_current: false, simulate_contact_success: true}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "state='APPROACHING'" <<<"${_rg_out}"

deadline=$((SECONDS + 40))
while true; do
  dock_output="$(
    service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/get_dock_state robot_interfaces_mission/srv/GetDockState "{dock_id: main_dock}" \
      2>"${LOG_DIR}/get_dock_state.err" || true
  )"
  if grep -q "success=True" <<<"${dock_output}" && grep -q "state='CHARGING'" <<<"${dock_output}"; then
    break
  fi
  if ((SECONDS >= deadline)); then
    echo "dock did not reach CHARGING state: ${dock_output}" >&2
    exit 1
  fi
  sleep 1
done

odom_x="$(read_odom_field pose.pose.position.x)"
odom_y="$(read_odom_field pose.pose.position.y)"
awk -v x="${odom_x}" -v y="${odom_y}" 'BEGIN { exit !(x < -2.0 && y < -2.0) }' || {
  echo "docking mission did not move Gazebo robot near dock: x=${odom_x}, y=${odom_y}" >&2
  exit 1
}

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/set_payload_loaded robot_interfaces_facility/srv/SetPayloadLoaded \
  "{loaded: true, payload_id: sim_tote, weight_kg: 8.5, message: rviz visual demo}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "payload_id='sim_tote'" <<<"${_rg_out}"
rg "loaded=True" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/request_station_confirmation robot_interfaces_facility/srv/RequestStationConfirmation \
  "{confirmation_id: sim_load_confirm, station_id: receiving, action: load_tote, mission_id: sim_visual, operator_hint: confirm simulated loading, timeout_sec: 0, timeout_policy: manual}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "confirmation_id='sim_load_confirm'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/block_station_route robot_interfaces_mission/srv/BlockStationRoute \
  "{from_station: receiving, to_station: storage_a, reason: sim_visual_block}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "route_edge:receiving__storage_a" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/request_lift_session robot_interfaces_facility/srv/RequestLiftSession \
  "{session_id: sim_lift, lift_id: freight_elevator, requester_id: sim_visual, from_station: packing, to_station: floor_2_dropoff, release_existing_for_requester: true}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "freight_elevator" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/reserve_facility_resource robot_interfaces_facility/srv/ReserveFacilityResource \
  "{resource_id: receiving_door, holder_id: sim_visual_door, mission_id: sim_visual, release_existing_for_holder: false}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "receiving_door" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/set_dynamic_speed_limit robot_interfaces_navigation/srv/SetDynamicSpeedLimit \
  "{speed_limit_mps: 0.25, reason: sim_visual_slow_zone}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "0.25" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/report_obstacle_blockage robot_interfaces_navigation/srv/ReportObstacleBlockage \
  "{blockage_id: sim_obstacle, reason: simulated obstacle, pause_active: false}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "sim_obstacle" <<<"${_rg_out}"

wait_marker_text "charging dock: CHARGING" 30
wait_marker_text "payload on robot: sim_tote" 30
wait_marker_text "confirm: load_tote" 30
wait_marker_text "blocked: sim_obstacle" 30
wait_marker_text "speed limit: 0.25 mps" 30
wait_marker_text "elevator: lift_session" 30
# 任务看板（amr_sim_visualizer_node 的 AddMissionPanel）从初始提交起就是中英双语的"当前任务 task: …"，
# 没有 "mission: " 前缀；原断言从未匹配过（4.1 定性：脚本过时）。只用英文部分匹配：ros2 topic echo 可能把中文转义成 \uXXXX。
wait_marker_text "task_board" 30
wait_marker_text " task: " 30

echo "amr simulation visualization passed"
