#!/usr/bin/env bash
# 阶段 7 专项。每项使用新的 ROS 域，并清理本次启动的进程组。
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${1:?output dir}"
mkdir -p "${OUT_DIR}/ops-evidence" || exit 1
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"
TABLE="${SCRIPT_DIR}/console_ops_cases.tsv"
RESULTS="${OUT_DIR}/console_ops_results.tsv"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

set +u
source "${ROOT_DIR}/install/setup.bash" || exit 1
set -u
source "${SCRIPT_DIR}/robot_wait.sh"

STACK_PID=""
cleanup() {
  [[ -n "${STACK_PID}" ]] || return 0
  kill -INT -- "-${STACK_PID}" 2>/dev/null || true
  for _ in {1..15}; do
    kill -0 -- "-${STACK_PID}" 2>/dev/null || break
    sleep 0.2
  done
  kill -TERM -- "-${STACK_PID}" 2>/dev/null || true
  sleep 0.2
  kill -KILL -- "-${STACK_PID}" 2>/dev/null || true
  wait "${STACK_PID}" 2>/dev/null || true
  STACK_PID=""
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

start_stack() {
  local stack="$1" name="$2" directory="$3"
  case "${stack}" in
    core|traffic_core)
      local mission_args=()
      if [[ "${stack}" == traffic_core ]]; then
        mission_args+=(traffic_intersection_locks_enabled:=false traffic_deadlock_resolution_enabled:=true)
      fi
      setsid bash -c '
        script_dir="$1"; scenario="$2"; evidence="$3"; shift 3
        source "${script_dir}/robot_wait.sh"
        ros2 run robot_tasks navigate_sequence_server_node --ros-args -p use_nav2_action:=false -p simulate_without_nav2:=true &
        ros2 launch robot_tasks mission_runner.launch.py autostart:=false return_to_dock_on_low_battery:=false "$@" &
        if [[ "${scenario}" == fleet_submit_local ]]; then
          STATION_AUTO_CONFIRM_LOG="${evidence}/auto-confirm.log" start_station_auto_confirm
        fi
        wait
      ' _ "${SCRIPT_DIR}" "${name}" "${directory}" "${mission_args[@]}" > "${directory}/stack.log" 2>&1 &
      STACK_PID=$!
      wait_service_type /v2/get_operator_snapshot robot_interfaces_business/srv/GetOperatorSnapshot 25 "${directory}/ready.log"
      ;;
    fleet_sim)
      setsid ros2 launch robot_bringup fleet_sim.launch.py fleet_size:=2 network:=workshop_2way         > "${directory}/stack.log" 2>&1 &
      STACK_PID=$!
      wait_service_type /v2/get_operator_snapshot robot_interfaces_business/srv/GetOperatorSnapshot 30 "${directory}/ready.log" &&
        wait_topic_type /robot_1/odom nav_msgs/msg/Odometry 25 "${directory}/odom.log"
      ;;
    delay_fixture)
      printf 'QtTest 延迟回执夹具随测试进程启动\n' > "${directory}/stack.log"
      ;;
    *) return 1 ;;
  esac
}

printf 'suite\tcase\tkind\tstatus\tseconds\n' > "${RESULTS}"
result=0
domain=${ROS_DOMAIN_ID:-$((100 + RANDOM % 50))}
while IFS=$'\t' read -r suite name binary stack budget timeout_s origin; do
  [[ "${suite}" == ops-ui ]] || continue
  # 用例之间增加域 ID。整批范围保留在 DDS 有效区内。
  export ROS_DOMAIN_ID="${domain}"
  domain=$(( (domain + 1) % 230 ))
  directory="${OUT_DIR}/ops-evidence/${name}"
  mkdir -p "${directory}" || exit 1
  printf 'case\t%s\nkind\t%s\nros_domain_id\t%s\nrevision\t%s\n'     "${name}" "${stack}" "${ROS_DOMAIN_ID}" "${NIGHTLY_REV:-local}" > "${directory}/run.tsv"
  start=${SECONDS}
  rc=0
  if start_stack "${stack}" "${name}" "${directory}"; then
    timeout --kill-after=10 "${timeout_s}" ros2 run robot_console "${binary}" "${name}"       -o -,txt -o "${directory}/test.xml,junitxml" > "${directory}/console.log" 2>&1 || rc=$?
  else
    printf '初始化仿真栈失败，查看 stack.log 和 ready.log\n' > "${directory}/console.log"
    rc=1
  fi
  cleanup
  printf 'exit_code\t%s\n' "${rc}" >> "${directory}/run.tsv"
  verdict=PASS
  if (( rc != 0 )); then
    verdict=FAIL
    result=1
  fi
  printf '%s\t%s\t%s\t%s\t%d\n' "${suite}" "${name}" "${stack}" "${verdict}" "$((SECONDS - start))" >> "${RESULTS}"
done < "${TABLE}"
exit "${result}"
