#!/usr/bin/env bash
# self 用例公共部分：载入工作区环境、自选 DDS 域、后台起节点（start_bg 名称 命令...），
# 退出时停掉自起进程；失败时把各节点日志的最后 60 行打进用例日志。
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=robot_wait.sh
source "${SCRIPT_DIR}/robot_wait.sh"
set +u
# shellcheck disable=SC1091  # 运行时才有的工作区环境
source "${ROOT_DIR}/install/setup.bash"
set -u
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 20))}"

LOG_DIR="$(mktemp -d /tmp/robot_self_case.XXXXXX)"
PIDS=()
self_cleanup() {
  local status=$?
  if (( status != 0 )); then
    for f in "${LOG_DIR}"/*.log; do
      [[ -f "${f}" ]] || continue
      echo "===== tail ${f##*/} =====" >&2
      tail -n 60 "${f}" >&2
    done
  fi
  stop_all_bg
  rm -rf "${LOG_DIR}"
}
trap self_cleanup EXIT

start_bg() {
  local name="$1"
  shift
  setsid "$@" >"${LOG_DIR}/${name}.log" 2>&1 < /dev/null &
  PIDS+=("$!")
}
stop_all_bg() {
  local pid
  for pid in "${PIDS[@]:-}"; do
    [[ -n "${pid}" ]] && kill -INT -- "-${pid}" >/dev/null 2>&1
  done
  sleep 2
  for pid in "${PIDS[@]:-}"; do
    [[ -n "${pid}" ]] && kill -KILL -- "-${pid}" >/dev/null 2>&1
  done
  PIDS=()
}

# 取话题的一条消息（带超时）。用法：topic_once <话题> <类型> [--field 字段]
topic_once() {
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once "$@"
}
