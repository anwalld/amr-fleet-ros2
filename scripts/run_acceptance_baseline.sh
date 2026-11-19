#!/usr/bin/env bash
# 逐个运行用例表 scripts/acceptance_cases.tsv 中的全部验收用例（单个失败不中断），输出通过/失败表。
# 用法：scripts/run_acceptance_baseline.sh [输出目录] [套件...]
#   默认输出到 /tmp/acceptance_<时间戳>，默认跑用例表中的全部套件。
# 结果：<输出目录>/results.tsv（suite case kind status seconds，kind 为用例声明的栈），
#       每个用例的日志 <套件>__<用例>.log 与命令跟踪 <套件>__<用例>.trace。
#
# 每个用例都从干净状态开始：跑之前、跑之后都清掉全部 ROS / Gazebo 进程。
# 用例需要的栈由用例表的 stack 列声明（acceptance-suite-rework design D4），不再按脚本内容猜：
#   none          不起栈（静态检查、lint）
#   self          脚本自己 ros2 launch，并自选 ROS_DOMAIN_ID
#   core          任务中枢默认配置（warehouse 站点、robot_navigation 地图分区）+ 无 Nav2 的导航序列服务
#   amr_demo      headless 演示栈，不自动开跑演示
#   mock_bringup  bringup.launch.py mode:=hardware backend:=mock + map_manager.launch.py
#   fleet_sim     2 车轻量仿真（workshop_2way 路网）
# 需要栈的用例：随机选一个 ROS_DOMAIN_ID（栈与用例共用），单独拉起栈，就绪后运行，结束后关掉。
# 每个用例的超时取用例表的 timeout_s；失败或超时都不重跑。
#
# 环境变量：
#   ONLY_CASES="suite:case ..."  只跑列出的用例（空则全跑）
#   STACK_WARMUP                 栈就绪最长等待 STACK_WARMUP*3 秒（默认 45，即 135 秒）
#   ACCEPTANCE_CASES_FILE        用例表路径（默认 scripts/acceptance_cases.tsv）
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${1:-/tmp/acceptance_$(date +%Y%m%d_%H%M%S)}"
shift || true
SUITES=("$@")
STACK_WARMUP="${STACK_WARMUP:-45}"
ONLY_CASES="${ONLY_CASES:-}"
CASES_FILE="${ACCEPTANCE_CASES_FILE:-${SCRIPT_DIR}/acceptance_cases.tsv}"
TRACE_TAIL_LINES=20

[[ -f "${CASES_FILE}" ]] || { echo "用例表不存在：${CASES_FILE}" >&2; exit 1; }

mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"
RESULTS="${OUT_DIR}/results.tsv"
printf "suite\tcase\tkind\tstatus\tseconds\n" > "${RESULTS}"

# 用例表的数据行（去掉 # 注释和表头）：suite case script stack budget_s timeout_s origin
case_rows() {
  grep -v '^#' "${CASES_FILE}" | awk -F'\t' 'NF && $1 != "suite"'
}
if [[ ${#SUITES[@]} -eq 0 ]]; then
  while IFS= read -r s; do SUITES+=("${s}"); done < <(case_rows | awk -F'\t' '!seen[$1]++ { print $1 }')
fi

# 命令跟踪（design D6）：用例的每个 bash 进程经 BASH_ENV 载入这段初始化，
# 把 set -x 的输出写到 ACCEPTANCE_TRACE_FILE，不混进标准输出。
XTRACE_INIT="${OUT_DIR}/.xtrace_init.sh"
cat > "${XTRACE_INIT}" <<'EOF'
if [[ -n "${ACCEPTANCE_TRACE_FILE:-}" ]]; then
  exec 19>>"${ACCEPTANCE_TRACE_FILE}"
  BASH_XTRACEFD=19
  PS4='+ ${BASH_SOURCE[0]##*/}:${LINENO}: '
  set -x
fi
EOF

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

# 各栈的启动命令（在同一个进程组里起，关栈时整组杀掉）。
stack_command() {
  case "$1" in
    core)
      echo "ros2 run robot_tasks navigate_sequence_server_node --ros-args -p use_nav2_action:=false -p simulate_without_nav2:=true &
            ros2 launch robot_tasks mission_runner.launch.py autostart:=false &
            wait" ;;
    amr_demo)
      echo "ros2 launch robot_bringup amr_demo.launch.py gui:=false use_rviz:=false auto_demo:=false" ;;
    mock_bringup)
      echo "ros2 launch robot_bringup bringup.launch.py mode:=hardware backend:=mock &
            ros2 launch robot_navigation map_manager.launch.py &
            wait" ;;
    fleet_sim)
      echo "ros2 launch robot_bringup fleet_sim.launch.py fleet_size:=2 network:=workshop_2way" ;;
    *) return 1 ;;
  esac
}

# 各栈的就绪判定（一次探测；每个查询都带超时）。
q() { timeout 10 "$@" 2>/dev/null; }
stack_probe() {
  case "$1" in
    core)
      [[ -n "$(q ros2 service type /v2/submit_order)" && -n "$(q ros2 topic type /mission_runner/state)" ]] ;;
    amr_demo)
      [[ -n "$(q ros2 service type /v2/enqueue_mission_profile)" && -n "$(q ros2 topic type /mission_runner/state)" ]] ;;
    mock_bringup)
      [[ -n "$(q ros2 service type /v2/set_cmd_source)" && -n "$(q ros2 service type /v2/list_maps)" ]] &&
        timeout 6 ros2 run tf2_ros tf2_echo odom base_footprint 2>/dev/null | grep -q "Translation" ;;
    fleet_sim)
      [[ -n "$(q ros2 service type /v2/submit_order)" && -n "$(q ros2 topic type /robot_1/odom)" &&
         -n "$(q ros2 topic type /robot_2/odom)" ]] ;;
    *) return 1 ;;
  esac
}

STACK_PGID=""
stack_up() {
  local stack="$1" tag="$2" cmd
  cmd="$(stack_command "${stack}")" || { echo "[runner] 未知的栈：${stack}" >&2; return 1; }
  setsid bash -c "${cmd}" > "${OUT_DIR}/_stack__${tag}.log" 2>&1 < /dev/null &
  STACK_PGID=$!
  ros2 daemon start >/dev/null 2>&1 < /dev/null
  # 连续 3 次探测成功才算就绪，避免 daemon 刚起时的抖动；超时也照常运行用例。
  local t0=${SECONDS} deadline=$(( SECONDS + STACK_WARMUP * 3 )) ready=0 hits=0
  while (( SECONDS < deadline )); do
    if stack_probe "${stack}"; then
      hits=$((hits + 1)); (( hits >= 3 )) && { ready=1; break; }
    else
      hits=0
    fi
    sleep 2
  done
  echo "[runner] stack=${stack} domain=${ROS_DOMAIN_ID} ready=${ready} after $((SECONDS - t0))s" \
    >> "${OUT_DIR}/_stack__${tag}.log"
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
  while IFS=$'\t' read -r -u 3 _ c script stack _ timeout_s _; do
    [[ -n "${ONLY_CASES}" && " ${ONLY_CASES} " != *" ${suite}:${c} "* ]] && continue
    tag="${suite}__${c}"
    log="${OUT_DIR}/${tag}.log"
    trace="${OUT_DIR}/${tag}.trace"
    : > "${trace}"

    case "${stack}" in
      none|self) unset ROS_DOMAIN_ID ;;
      *)
        export ROS_DOMAIN_ID="$(( RANDOM % 150 + 50 ))"
        stack_up "${stack}" "${tag}" ;;
    esac

    start=$(date +%s)
    ACCEPTANCE_TRACE_FILE="${trace}" BASH_ENV="${XTRACE_INIT}" \
      setsid timeout --kill-after=30 "${timeout_s}" "${SCRIPT_DIR}/check_robot.sh" "${suite}" "${c}" \
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
    if [[ "${status}" != PASS ]]; then
      {
        echo
        echo "[runner] ${status}（脚本 ${script}，栈 ${stack}，超时 ${timeout_s}s）；命令跟踪最后 ${TRACE_TAIL_LINES} 行（全文 ${trace}）："
        tail -n "${TRACE_TAIL_LINES}" "${trace}"
      } >> "${log}"
    fi
    printf "%s\t%s\t%s\t%s\t%s\n" "${suite}" "${c}" "${stack}" "${status}" "${secs}" | tee -a "${RESULTS}"
  done 3< <(case_rows | awk -F'\t' -v s="${suite}" '$1 == s')
done
echo "PASS=${pass} FAIL=${fail} -> ${RESULTS}"
