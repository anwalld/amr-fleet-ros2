#!/usr/bin/env bash
# 阶段 7 Alarm 专项，保留真实窗口、仿真栈与安装入口的日志。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${1:?output dir}/alarm-evidence"
mkdir -p "${OUT_DIR}"
set +u
source "${ROOT_DIR}/install/setup.bash"
set -u
source "${SCRIPT_DIR}/robot_wait.sh"

export QT_QPA_PLATFORM=offscreen
export ROS_DOMAIN_ID="$((120 + RANDOM % 40))"
stack_pid=""
cleanup() {
  if [[ -n "${stack_pid}" ]]; then
    kill -INT -- "-${stack_pid}" 2>/dev/null || true
    for unused in {1..25}; do
      kill -0 -- "-${stack_pid}" 2>/dev/null || break
      sleep 0.2
    done
    kill -TERM -- "-${stack_pid}" 2>/dev/null || true
    sleep 0.2
    kill -KILL -- "-${stack_pid}" 2>/dev/null || true
    wait "${stack_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'case\tresult\n' > "${OUT_DIR}/results.tsv"
result=0
run_case() {
  local name="$1" rc=0
  shift
  "$@" > "${OUT_DIR}/${name}.log" 2>&1 || rc=$?
  if (( rc == 0 )); then
    printf '%s\tPASS\n' "${name}" >> "${OUT_DIR}/results.tsv"
  else
    printf '%s\tFAIL(%s)\n' "${name}" "${rc}" >> "${OUT_DIR}/results.tsv"
    result=1
  fi
}

qt_case() {
  local name="$1"
  timeout --kill-after=10 120 ros2 run robot_console console_ops_acceptance "${name}" \
    -o -,txt -o "${OUT_DIR}/${name}.xml,junitxml"
}

# 连接和告警夹具只在无车队栈的独立 DDS 域运行。
run_case alarm_events_and_recovery qt_case alarm_events_and_recovery

export ROS_DOMAIN_ID="$((ROS_DOMAIN_ID + 40))"
setsid ros2 launch robot_bringup fleet_sim.launch.py fleet_size:=2 network:=workshop_2way \
  fleet_sim_stop_enabled:=true > "${OUT_DIR}/fleet.log" 2>&1 &
stack_pid=$!

fleet_case() {
  wait_service_type /v2/set_fleet_sim_stop std_srvs/srv/SetBool 20 "${OUT_DIR}/stop-ready.log" &&
    wait_topic_type /fleet/sim_stop/state std_msgs/msg/Bool 20 "${OUT_DIR}/stop-topic.log" &&
    qt_case alarm_sim_stop
}

installation_case() {
  local desktop_dir="${OUT_DIR}/desktop"
  timeout 10 ros2 run robot_console install_desktop_entry "${ROOT_DIR}/install/setup.bash" --app-dir "${desktop_dir}" &&
    test -s "${desktop_dir}/robot-console.desktop" &&
    test -s "${desktop_dir}/robot-console-replay.desktop" &&
    timeout --kill-after=5 20 "${desktop_dir}/robot-console.sh" --smoke-seconds 2 --smoke-out "${OUT_DIR}/installed_live.tsv" &&
    test -s "${OUT_DIR}/installed_live.tsv" &&
    timeout --kill-after=5 15 "${desktop_dir}/robot-console-replay.sh" \
      "${ROOT_DIR}/src/robot_console_core/test/data/mini.jsonl" --quit-after 2
}

run_case alarm_sim_stop fleet_case
run_case alarm_installation installation_case
exit "${result}"
