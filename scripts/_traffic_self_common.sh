#!/usr/bin/env bash
# 交通类 self 用例的公共部分：自起任务中枢（参数由调用方给出），带超时的服务调用，失败时打印节点日志末尾。
# 用法：source 本文件后调用 start_mission_runner <launch 参数...>
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=robot_wait.sh
source "${SCRIPT_DIR}/robot_wait.sh"
set +u
# shellcheck disable=SC1091  # 运行时才有的工作区环境
source "${ROOT_DIR}/install/setup.bash"
set -u
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 20))}"

LOG_DIR="$(mktemp -d /tmp/robot_traffic_case.XXXXXX)"
PIDS=()
traffic_cleanup() {
  local status=$?
  if (( status != 0 )); then
    for f in "${LOG_DIR}"/*.log; do
      [[ -f "${f}" ]] || continue
      echo "===== tail ${f##*/} =====" >&2
      tail -n 60 "${f}" >&2
    done
  fi
  for pid in "${PIDS[@]:-}"; do
    kill "${pid}" >/dev/null 2>&1 || true
  done
  wait "${PIDS[@]:-}" >/dev/null 2>&1 || true
  rm -rf "${LOG_DIR}"
}
trap traffic_cleanup EXIT

start_mission_runner() {
  ros2 launch robot_tasks mission_runner.launch.py autostart:=false return_to_dock_on_low_battery:=false "$@" \
    >"${LOG_DIR}/mission_runner.log" 2>&1 &
  PIDS+=("$!")
  wait_service_type /v2/request_route_segment "robot_interfaces_mission/srv/RequestRouteSegment" 30 "${LOG_DIR}/wait_request.err"
  wait_service_type /v2/detect_traffic_deadlock "robot_interfaces_mission/srv/DetectTrafficDeadlock" 30 "${LOG_DIR}/wait_detect.err"
}

svc() { timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "$@"; }

# request_path <robot> <goal_station> <站点...>：两站时为单段申请（rolling）；带 PATH=1 时按整条路径申请。
request_segment() {
  local robot="$1" from="$2" to="$3" goal="${4:-}"
  svc /v2/request_route_segment robot_interfaces_mission/srv/RequestRouteSegment \
    "{robot_id: '${robot}', from_station: '${from}', to_station: '${to}', priority: 10, station_path: [], goal_station: '${goal}'}"
}
request_whole_path() {
  local robot="$1"
  shift
  svc /v2/request_route_segment robot_interfaces_mission/srv/RequestRouteSegment \
    "{robot_id: '${robot}', from_station: '$1', to_station: '${!#}', priority: 10, station_path: $(yaml_flow_list "$@")}"
}
detect() { svc /v2/detect_traffic_deadlock robot_interfaces_mission/srv/DetectTrafficDeadlock "{}"; }
