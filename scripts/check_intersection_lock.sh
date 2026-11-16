#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 20))}"

LOG_DIR="$(mktemp -d /tmp/robot_intersection_lock.XXXXXX)"
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
  traffic_intersection_locks_enabled:=true

wait_submit_order 20 "${LOG_DIR}/wait_submit.err"
wait_service_type /v2/list_traffic_reservations "robot_interfaces_mission/srv/ListTrafficReservations" 20 \
  "${LOG_DIR}/wait_list_traffic.err"
wait_service_type /v2/cancel_queued_mission "robot_interfaces_mission/srv/CancelQueuedMission" 20 \
  "${LOG_DIR}/wait_cancel.err"

# M2 滚动申请语义：路口锁仍然生效，但拿不到不再拒绝，而是按车辆号排队，
# 前车释放后自动授予队首。旧语义（whole_route）下 beta 入队时就被拒。

# 1. 任务层：两单都能入队（入队只校验路径存在，不锁路）。
call_submit_order intersection_alpha station_transport 10 \
  "pickup_station=receiving;dropoff_station=storage_a;start_if_idle=false;preempt_current=false" \
  station_transport_order \
  >"${LOG_DIR}/intersection_alpha.out"
rg "accepted=True" "${LOG_DIR}/intersection_alpha.out"
rg "queue_size=1" "${LOG_DIR}/intersection_alpha.out"

call_submit_order intersection_beta station_transport 10 \
  "pickup_station=storage_a;dropoff_station=packing;start_if_idle=false;preempt_current=false" \
  station_transport_order \
  >"${LOG_DIR}/intersection_beta.out"
rg "accepted=True" "${LOG_DIR}/intersection_beta.out"
rg "queue_size=2" "${LOG_DIR}/intersection_beta.out"

# 2. 路权层：alpha 车占住 receiving -> storage_a（路段 + storage_a 路口）。
wait_service_type /v2/request_route_segment "robot_interfaces_mission/srv/RequestRouteSegment" 20 \
  "${LOG_DIR}/wait_request.err"
call_request_route_segment robot_alpha receiving storage_a 10 >"${LOG_DIR}/alpha_seg.out"
rg "granted=True" "${LOG_DIR}/alpha_seg.out"
rg "route_node:storage_a" "${LOG_DIR}/alpha_seg.out"

ros2 service call /v2/list_traffic_reservations robot_interfaces_mission/srv/ListTrafficReservations "{}" \
  >"${LOG_DIR}/traffic.out"
rg "success=True" "${LOG_DIR}/traffic.out"
rg "route_node:storage_a" "${LOG_DIR}/traffic.out"
rg "robot_alpha" "${LOG_DIR}/traffic.out"

# 3. beta 车要从 packing 开到 storage_a，路口被占 -> 排队（不是拒绝）。
call_request_route_segment robot_beta packing storage_a 10 >"${LOG_DIR}/beta_seg.out"
rg "granted=False" "${LOG_DIR}/beta_seg.out"
rg "queued=True" "${LOG_DIR}/beta_seg.out"
rg "route_node:storage_a" "${LOG_DIR}/beta_seg.out"
rg "robot_alpha" "${LOG_DIR}/beta_seg.out"

# 4. 监听授予通知，alpha 释放后 beta 自动拿到路口。
timeout 20s ros2 topic echo --once /traffic/grants robot_interfaces_mission/msg/TrafficGrant \
  >"${LOG_DIR}/grant.out" 2>&1 &
grant_pid=$!
sleep 4
call_release_route_segment robot_alpha >"${LOG_DIR}/alpha_release.out"
rg "success=True" "${LOG_DIR}/alpha_release.out"
rg "granted_robot_ids=\['robot_beta'\]" "${LOG_DIR}/alpha_release.out"
wait "${grant_pid}" || true
rg "robot_beta" "${LOG_DIR}/grant.out"
rg "granted" "${LOG_DIR}/grant.out"

ros2 service call /v2/list_traffic_reservations robot_interfaces_mission/srv/ListTrafficReservations "{}" \
  >"${LOG_DIR}/traffic_after.out"
rg "route_node:storage_a" "${LOG_DIR}/traffic_after.out"
rg "robot_beta" "${LOG_DIR}/traffic_after.out"
if rg "robot_alpha" "${LOG_DIR}/traffic_after.out" >/dev/null; then
  echo "robot_alpha still owns traffic resources after release" >&2
  exit 1
fi

echo "intersection lock passed"
