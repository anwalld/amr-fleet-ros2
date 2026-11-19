#!/usr/bin/env bash
# 任务失败恢复：导航失败 → 自动重试 1 次（failure_retry_limit 默认 1）→ 仍失败 → NEEDS_OPERATOR，
# 恢复状态里重试计数为 1、恢复动作为 manual；重置后状态清零、可再次开跑。
# 自起任务中枢和"开着 Nav2 动作但没有 Nav2、且不允许模拟"的导航序列服务，导航每次都稳定失败
# （amr_demo 里导航正常，任务直接 FINISHED，测不到恢复流程）。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 200 + 20))}"

LOG_DIR="$(mktemp -d /tmp/robot_mission_recovery.XXXXXX)"
PIDS=()
cleanup() {
  local status=$?
  # 失败时把自起节点日志的末尾打进用例日志，否则临时目录一删就没有现场了。
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
trap cleanup EXIT
start_bg() {
  local name="$1"
  shift
  "$@" >"${LOG_DIR}/${name}.log" 2>&1 &
  PIDS+=("$!")
}

start_bg navigate_sequence_server ros2 run robot_tasks navigate_sequence_server_node --ros-args \
  -p use_nav2_action:=true \
  -p simulate_without_nav2:=false \
  -p nav2_server_timeout_ms:=1000
start_bg mission_runner ros2 launch robot_tasks mission_runner.launch.py \
  autostart:=false \
  return_to_dock_on_low_battery:=false

wait_service_type /start_mission_profile "std_srvs/srv/Trigger" 30 "${LOG_DIR}/wait_start.err"
wait_service_type /v2/get_mission_recovery_state "robot_interfaces_mission/srv/GetMissionRecoveryState" 30 "${LOG_DIR}/wait_recovery.err"
wait_service_type /reset_mission_recovery_state "std_srvs/srv/Trigger" 30 "${LOG_DIR}/wait_reset.err"
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 30 "${LOG_DIR}/wait_state.err"

trigger() {
  local out
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "$1" std_srvs/srv/Trigger "{}")"
  echo "${out}"
  rg "success=True" <<<"${out}"
}
recovery_state() {
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_mission_recovery_state \
    robot_interfaces_mission/srv/GetMissionRecoveryState "{}"
}

trigger /reset_mission_recovery_state
trigger /start_mission_profile
wait_active_mission demo_patrol 60 "NEEDS_OPERATOR"

out="$(recovery_state)"
echo "${out}"
rg "success=True" <<<"${out}"
rg "retry_count=1" <<<"${out}"
rg "last_recovery_action='manual'" <<<"${out}"

trigger /reset_mission_recovery_state
out="$(recovery_state)"
echo "${out}"
rg "retry_count=0" <<<"${out}"

echo "mission recovery passed"
