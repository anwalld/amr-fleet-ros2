#!/usr/bin/env bash
# 验收用例 safety:traffic_deadlock（M3 起按等待环语义判定）。
#   1. 两车只是共用站点、没有等待（M2 时代的"冲突候选"）：deadlocked=False。
#   2. 关闭死锁解除后构造两车互等（各占一条路段、各等对方那条）：deadlocked=True，
#      两车都在 mission_ids 里。
# 所有 ros2 service call 都带 timeout：FastDDS 高负载下会丢服务应答，不带超时会挂到用例上限。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 20))}"

LOG_DIR="$(mktemp -d /tmp/robot_traffic_deadlock.XXXXXX)"
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

SVC_TIMEOUT="${SVC_TIMEOUT:-20}"

svc() {
  service_response "${SVC_TIMEOUT}" "$@"
}

request_segment() {
  local robot_id="$1" from_station="$2" to_station="$3"
  svc /v2/request_route_segment robot_interfaces_mission/srv/RequestRouteSegment \
    "{robot_id: $(yaml_single_quote "${robot_id}"), from_station: $(yaml_single_quote "${from_station}"), to_station: $(yaml_single_quote "${to_station}"), priority: 10, station_path: []}"
}

detect() {
  svc /v2/detect_traffic_deadlock robot_interfaces_mission/srv/DetectTrafficDeadlock "{}"
}

# 关闭死锁解除（只检测，等待环保持原样，便于判定）；关闭路口锁（两车可各占一条通向同一站点的路段）。
start_bg mission_runner ros2 launch robot_tasks mission_runner.launch.py \
  autostart:=false \
  return_to_dock_on_low_battery:=false \
  traffic_intersection_locks_enabled:=false \
  traffic_deadlock_resolution_enabled:=false

wait_submit_order 20 "${LOG_DIR}/wait_submit.err"
wait_service_type /v2/detect_traffic_deadlock "robot_interfaces_mission/srv/DetectTrafficDeadlock" 20 \
  "${LOG_DIR}/wait_detect.err"
wait_service_type /v2/request_route_segment "robot_interfaces_mission/srv/RequestRouteSegment" 20 \
  "${LOG_DIR}/wait_request.err"

# 1. 共用站点但无等待：两车各拿一条通向 storage_a 的路段，不报死锁。
request_segment robot_alpha receiving storage_a >"${LOG_DIR}/alpha_seg.out"
rg "granted=True" "${LOG_DIR}/alpha_seg.out"
request_segment robot_beta packing storage_a >"${LOG_DIR}/beta_seg.out"
rg "granted=True" "${LOG_DIR}/beta_seg.out"
detect >"${LOG_DIR}/shared_detect.out"
rg "success=True" "${LOG_DIR}/shared_detect.out"
rg "deadlocked=False" "${LOG_DIR}/shared_detect.out"

# 2. 两车互等：alpha 占着 receiving–storage_a 去等 beta 的 packing–storage_a，beta 反之。
request_segment robot_alpha storage_a packing >"${LOG_DIR}/alpha_wait.out"
rg "queued=True" "${LOG_DIR}/alpha_wait.out"
request_segment robot_beta storage_a receiving >"${LOG_DIR}/beta_wait.out"
rg "queued=True" "${LOG_DIR}/beta_wait.out"

detect >"${LOG_DIR}/deadlock_detect.out"
rg "success=True" "${LOG_DIR}/deadlock_detect.out"
rg "deadlocked=True" "${LOG_DIR}/deadlock_detect.out"
rg "robot_alpha" "${LOG_DIR}/deadlock_detect.out"
rg "robot_beta" "${LOG_DIR}/deadlock_detect.out"
rg "storage_a" "${LOG_DIR}/deadlock_detect.out"

echo "traffic deadlock detection passed"
