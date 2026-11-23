#!/usr/bin/env bash
# 调度台冒烟（M4，夜跑阶段 7）：offscreen 的 robot_console 对着 6 车 rolling 压测栈跑 5 分钟。
# 用法：scripts/run_console_smoke.sh <输出目录>
# 环境变量：
#   SMOKE_SECONDS=300            调度台计时秒数（从同时收到第一份快照和路网开始算）
#   SMOKE_NETWORK=workshop_2way  压测路网
#   QT_QPA_PLATFORM              默认 offscreen
#
# 做法：后台用 run_fleet_traffic_stress.sh 拉起 6 车 rolling 轻量仿真并持续下单（与阶段 5 同源的负载），
# 前台以冒烟模式运行 robot_console，度量写到 console_metrics.tsv；跑完停掉压测进程组
# （压测脚本自己的 trap 会停掉 launch）。
# 结果：<输出目录>/console_smoke.tsv = 调度台输出的全部度量 + console_rc + verdict（+ reason）。
# 判定 PASS：调度台退出码 0，snapshot_hz 与 traffic_hz 都 >= 0.8，pose_hz_min >= 5，robots_with_pose >= 6，
#   且冒烟期间录下的 stack/console.jsonl 通过 console_replay_check.py（四类记录齐全、回放不跳行）。
# 延迟和帧率只记录，不参与判定（offscreen 下测得）。
# 其他输出：console.log（调度台日志）、stack/（压测输出）、stack.out。
# 退出码：0 = PASS，1 = FAIL。
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${1:?output dir}"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"
SMOKE_SECONDS="${SMOKE_SECONDS:-300}"
SMOKE_NETWORK="${SMOKE_NETWORK:-workshop_2way}"
SMOKE_ROBOTS=6
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

METRICS="${OUT_DIR}/console_metrics.tsv"
RESULT="${OUT_DIR}/console_smoke.tsv"
rm -f "${METRICS}" "${RESULT}"

set +u
# shellcheck disable=SC1091  # 运行时才有的工作区环境
source "${ROOT_DIR}/install/setup.bash"
set -u
# 压测栈和调度台必须在同一个 DDS 域里。
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 150 + 50))}"

minutes=$(( (SMOKE_SECONDS + 59) / 60 + 3 ))
# 冒烟期间同时录像，结束后回放校验（acceptance-suite-rework：调度台冒烟附带回放校验）。
CONSOLE_CAPTURE=1 NETWORK="${SMOKE_NETWORK}" setsid "${SCRIPT_DIR}/run_fleet_traffic_stress.sh" \
  "${SMOKE_ROBOTS}" "${minutes}" rolling "${OUT_DIR}/stack" \
  > "${OUT_DIR}/stack.out" 2>&1 < /dev/null &
STACK_PID=$!

# shellcheck disable=SC2329  # 由 trap 调用
cleanup() {
  [[ -n "${STACK_PID}" ]] || return 0
  kill -INT -- "-${STACK_PID}" 2>/dev/null
  for _ in $(seq 1 40); do
    kill -0 "${STACK_PID}" 2>/dev/null || break
    sleep 0.5
  done
  if kill -0 "${STACK_PID}" 2>/dev/null; then
    kill -KILL -- "-${STACK_PID}" 2>/dev/null
    # 压测脚本被强杀时来不及停 launch：按工作区路径兜底清理（方括号写法避免匹配到自己）。
    pkill -INT -f "[r]obot_bringup fleet_sim.launch.py" 2>/dev/null; sleep 3
    pkill -KILL -f "[r]obot_ws/install/" 2>/dev/null
  fi
  STACK_PID=""
}
trap cleanup EXIT

echo "[$(date +%T)] 压测栈已在后台拉起（${SMOKE_ROBOTS} 车 rolling，${SMOKE_NETWORK}，ROS_DOMAIN_ID=${ROS_DOMAIN_ID}）"
timeout --kill-after=10 $((SMOKE_SECONDS + 180)) \
  ros2 run robot_console robot_console --smoke-seconds "${SMOKE_SECONDS}" --smoke-out "${METRICS}" \
  > "${OUT_DIR}/console.log" 2>&1 < /dev/null
console_rc=$?
echo "[$(date +%T)] 调度台退出码 ${console_rc}"

metric() { awk -F'\t' -v k="$1" '$1 == k { print $2; found = 1 } END { if (!found) print "" }' "${METRICS}"; }
at_least() { awk -v v="$1" -v m="$2" 'BEGIN { exit !(v != "" && v + 0 >= m + 0) }'; }

reason=""
if [[ "${console_rc}" != "0" ]]; then
  reason="调度台退出码 ${console_rc}"
  [[ "${console_rc}" == "2" ]] && reason="${reason}（等待超时：没有同时收到快照和路网）"
  [[ "${console_rc}" == "124" ]] && reason="${reason}（超时被杀）"
elif [[ ! -s "${METRICS}" ]]; then
  reason="调度台没有写出度量文件"
else
  snapshot_hz="$(metric snapshot_hz)"
  traffic_hz="$(metric traffic_hz)"
  pose_hz_min="$(metric pose_hz_min)"
  robots_with_pose="$(metric robots_with_pose)"
  at_least "${snapshot_hz}" 0.8 || reason="${reason}snapshot_hz=${snapshot_hz} < 0.8; "
  at_least "${traffic_hz}" 0.8 || reason="${reason}traffic_hz=${traffic_hz} < 0.8; "
  at_least "${pose_hz_min}" 5 || reason="${reason}pose_hz_min=${pose_hz_min} < 5; "
  at_least "${robots_with_pose}" "${SMOKE_ROBOTS}" || \
    reason="${reason}robots_with_pose=${robots_with_pose} < ${SMOKE_ROBOTS}; "
fi

# 先停栈（采集进程随之收尾、写完文件），再校验录像：每行可解析、四类记录齐全、回放程序退出 0 且不跳行。
cleanup
REPLAY_BIN="${ROOT_DIR}/install/robot_console_core/bin/robot_console_replay"
if [[ ! -f "${REPLAY_BIN}" || ! -x "${REPLAY_BIN}" ]]; then
  replay_check="FAIL 回放程序不存在或不可执行：${REPLAY_BIN}"
  reason="${reason}录像回放校验：${replay_check}; "
elif ! replay_check="$(python3 "${SCRIPT_DIR}/console_replay_check.py" \
    "${OUT_DIR}/stack/console.jsonl" "${REPLAY_BIN}" 2>&1 | tail -1)"; then
  reason="${reason}录像回放校验：${replay_check}; "
fi

{
  if [[ -s "${METRICS}" ]]; then
    cat "${METRICS}"
  else
    printf "metric\tvalue\n"
  fi
  printf "console_rc\t%s\n" "${console_rc}"
  printf "replay_check\t%s\n" "${replay_check}"
  if [[ -z "${reason}" ]]; then
    printf "verdict\tPASS\n"
  else
    printf "verdict\tFAIL\n"
    printf "reason\t%s\n" "${reason}"
  fi
} > "${RESULT}"

echo "console smoke: $(awk -F'\t' '$1 == "verdict" { print $2 }' "${RESULT}") -> ${RESULT}"
[[ -z "${reason}" ]]
