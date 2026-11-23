#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 1))}"

LOG_DIR="$(mktemp -d /tmp/robot_dynamic_route_block.XXXXXX)"
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

# 这个用例验证"入队即锁整条路径"模式下封路、解封、清除预约的语义，
# M2 起固定用 whole_route 对照模式（rolling 下入队不锁路）。
start_bg mission_runner ros2 launch robot_tasks mission_runner.launch.py \
  autostart:=false \
  return_to_dock_on_low_battery:=false \
  traffic_reservation_mode:=whole_route

wait_service_type /v2/block_station_route "robot_interfaces_mission/srv/BlockStationRoute" 20 \
  "${LOG_DIR}/wait_block.err"
wait_service_type /v2/unblock_station_route "robot_interfaces_mission/srv/UnblockStationRoute" 20 \
  "${LOG_DIR}/wait_unblock.err"
wait_service_type /v2/list_traffic_reservations "robot_interfaces_mission/srv/ListTrafficReservations" 20 \
  "${LOG_DIR}/wait_list_traffic.err"
wait_service_type /v2/clear_traffic_reservation "robot_interfaces_mission/srv/ClearTrafficReservation" 20 \
  "${LOG_DIR}/wait_clear_traffic.err"
wait_submit_order 20 "${LOG_DIR}/wait_submit.err"
wait_service_type /v2/cancel_queued_mission "robot_interfaces_mission/srv/CancelQueuedMission" 20 \
  "${LOG_DIR}/wait_cancel.err"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/block_station_route robot_interfaces_mission/srv/BlockStationRoute \
  "{from_station: receiving, to_station: storage_a, reason: aisle maintenance}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "resource_id='route_edge:receiving__storage_a'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_traffic_reservations robot_interfaces_mission/srv/ListTrafficReservations "{}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "route_edge:receiving__storage_a" <<<"${_rg_out}"
rg "traffic_block:aisle_maintenance" <<<"${_rg_out}"
rg "blocked" <<<"${_rg_out}"

_rg_out="$(call_submit_order blocked_route_alpha station_transport 10 \
  "pickup_station=receiving;dropoff_station=storage_a;start_if_idle=false;preempt_current=false" \
  station_transport_order | response_only)"
rg "accepted=False" <<<"${_rg_out}"
rg "route resource locked" <<<"${_rg_out}"
rg "traffic_block:aisle_maintenance" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/unblock_station_route robot_interfaces_mission/srv/UnblockStationRoute \
  "{from_station: receiving, to_station: storage_a}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "resource_id='route_edge:receiving__storage_a'" <<<"${_rg_out}"

_rg_out="$(call_submit_order clear_route_alpha station_transport 10 \
  "pickup_station=receiving;dropoff_station=storage_a;start_if_idle=false;preempt_current=false" \
  station_transport_order | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_order_clear_route_alpha" <<<"${_rg_out}"
rg "queue_size=1" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_traffic_reservations robot_interfaces_mission/srv/ListTrafficReservations "{}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "route_edge:receiving__storage_a" <<<"${_rg_out}"
# M3 起路权占用者是车辆号（robot_1），不再是任务号。
rg "robot_1" <<<"${_rg_out}"
rg "mission" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/clear_traffic_reservation robot_interfaces_mission/srv/ClearTrafficReservation \
  "{resource_id: 'route_edge:receiving__storage_a'}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "owner_id='robot_1'" <<<"${_rg_out}"

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/cancel_queued_mission robot_interfaces_mission/srv/CancelQueuedMission \
  "{mission_id: station_order_clear_route_alpha, cancel_active: false}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "queue_size=0" <<<"${_rg_out}"

echo "dynamic route block passed"
