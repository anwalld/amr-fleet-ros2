#!/usr/bin/env bash
# 逐个运行 check_robot.sh 的全部验收用例（单个失败不中断），输出通过/失败表。
# 用法：scripts/run_acceptance_baseline.sh [输出目录] [套件...]
#   默认输出到 /tmp/acceptance_<时间戳>，默认跑全部套件。
# 结果：<输出目录>/results.tsv（suite case kind status seconds）与每个用例的日志。
#
# 每个用例都从干净状态开始：跑之前清掉全部 ROS / Gazebo 进程。
# 用例分三类：
#   self   脚本自己 ros2 launch（且自选随机 ROS_DOMAIN_ID 隔离）
#   live   对“已在运行的系统”做在线检查：为它单独拉起一套新的 headless 仿真栈
#   static 不依赖运行中系统的静态检查
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${1:-/tmp/acceptance_$(date +%Y%m%d_%H%M%S)}"
shift || true
SUITES=("$@")
[[ ${#SUITES[@]} -eq 0 ]] && SUITES=(mission facility amr_sim business safety ops)
CASE_TIMEOUT="${CASE_TIMEOUT:-600}"
STACK_WARMUP="${STACK_WARMUP:-45}"
# 只跑指定用例：ONLY_CASES="safety:route_locks mission:queue"（空则全跑）。
ONLY_CASES="${ONLY_CASES:-}"

mkdir -p "${OUT_DIR}"
RESULTS="${OUT_DIR}/results.tsv"
printf "suite\tcase\tkind\tstatus\tseconds\n" > "${RESULTS}"

# 从 check_robot.sh 中取出用例清单与脚本映射，避免重复维护。
eval "$(sed -n '/^suite_cases() {/,/^}/p' "${SCRIPT_DIR}/check_robot.sh")"
eval "$(sed -n '/^case_script() {/,/^}/p' "${SCRIPT_DIR}/check_robot.sh")"

# 杀掉一个进程组（先 INT/TERM 给 launch 机会收尾，再 KILL）。
kill_group() {
  local pgid="$1"
  kill -INT -- "-${pgid}" 2>/dev/null; sleep 3
  kill -TERM -- "-${pgid}" 2>/dev/null; sleep 2
  kill -KILL -- "-${pgid}" 2>/dev/null
}

# 清掉所有 ROS / Gazebo 相关进程（包括孤儿节点与各 domain 的 ros2 daemon）。
ros_sweep() {
  pkill -KILL -f "[/]opt/ros/jazzy/lib/" 2>/dev/null
  pkill -KILL -f "[r]obot_ws/install/" 2>/dev/null
  pkill -KILL -f "[g]z sim" 2>/dev/null
  pkill -KILL -f "[r]os2 (launch|run|topic|service|action|node|param)" 2>/dev/null
  pkill -KILL -f "[r]os2cli.daemon" 2>/dev/null
  pkill -KILL -f "[r]obot_ws/scripts/.*\.py" 2>/dev/null
  sleep 2
}

STACK_PGID=""
stack_up() {
  setsid ros2 launch robot_bringup amr_demo.launch.py gui:=false use_rviz:=false auto_demo:=false \
    > "${OUT_DIR}/_stack__${1}.log" 2>&1 < /dev/null &
  STACK_PGID=$!
  # 预热 ros2 daemon，并等到任务中枢的服务真正可见（最多 STACK_WARMUP*3 秒）。
  ros2 daemon start >/dev/null 2>&1
  local t0=${SECONDS} deadline=$(( SECONDS + STACK_WARMUP * 3 )) ready=0 hits=0
  # 用与验收脚本相同的查询判断就绪，且要求连续 3 次成功，避免 daemon 刚起时的抖动。
  while (( SECONDS < deadline )); do
    if [[ -n "$(ros2 service type /v2/enqueue_mission_profile 2>/dev/null)" && \
          -n "$(ros2 topic type /mission_runner/state 2>/dev/null)" ]]; then
      hits=$((hits + 1)); (( hits >= 3 )) && { ready=1; break; }
    else
      hits=0
    fi
    sleep 2
  done
  echo "[runner] stack ready=${ready} after $((SECONDS - t0))s" >> "${OUT_DIR}/_stack__${1}.log"
  sleep 5
}
stack_down() {
  [[ -n "${STACK_PGID}" ]] && kill_group "${STACK_PGID}"
  STACK_PGID=""
}
trap 'stack_down; ros_sweep' EXIT

ros_sweep
pass=0; fail=0
for suite in "${SUITES[@]}"; do
  for c in $(suite_cases "${suite}"); do
    [[ -n "${ONLY_CASES}" && " ${ONLY_CASES} " != *" ${suite}:${c} "* ]] && continue
    log="${OUT_DIR}/${suite}__${c}.log"
    script="${SCRIPT_DIR}/$(case_script "${suite}" "${c}")"
    if [[ -f "${script}" ]] && grep -qE "ros2 launch" "${script}"; then
      kind=self
    elif [[ -f "${script}" ]] && grep -qE "ros2 (service|topic|action|node|param)" "${script}"; then
      kind=live
    else
      kind=static
    fi

    [[ "${kind}" == live ]] && stack_up "${suite}__${c}"

    start=$(date +%s)
    setsid timeout --kill-after=30 "${CASE_TIMEOUT}" "${SCRIPT_DIR}/check_robot.sh" "${suite}" "${c}" \
      > "${log}" 2>&1 < /dev/null &
    case_pgid=$!
    wait "${case_pgid}"
    rc=$?
    secs=$(( $(date +%s) - start ))
    kill_group "${case_pgid}" 2>/dev/null
    stack_down
    ros_sweep

    case ${rc} in
      0) status=PASS; pass=$((pass+1)) ;;
      124|137) status=TIMEOUT; fail=$((fail+1)) ;;
      *) status="FAIL(${rc})"; fail=$((fail+1)) ;;
    esac
    printf "%s\t%s\t%s\t%s\t%s\n" "${suite}" "${c}" "${kind}" "${status}" "${secs}" | tee -a "${RESULTS}"
  done
done
echo "PASS=${pass} FAIL=${fail} -> ${RESULTS}"
