#!/usr/bin/env bash
# M3 死锁场景（夜跑阶段 6）：两车对向、三车成环、环外有车等待，各跑 N 遍。
# 用法：scripts/run_deadlock_scenarios.sh <输出目录> [遍数，默认 3]
# 环境变量：SCENARIOS="head_on ring3 outside_wait"（默认全部）、ROUND_TIMEOUT（每遍秒数上限，默认 420）
#
# 每遍整栈重启：以 start_stations 把车摆到场景需要的站点，拉起 fleet_sim（workshop_2way，
# 死锁解除开启，不开 Gazebo），再由 deadlock_scenarios.py 下单、判定，结果追加到
# <输出目录>/deadlock_scenarios.tsv（场景、遍次、是否检出、解除动作、让路车、解除耗时毫秒、结论）。
# 每遍的启动日志与判定输出：<输出目录>/scenario_<场景>_<遍次>.{launch.log,out}
# 服务调用都在 Python 里带超时完成，不用 ros2 service call。
# 退出码：0 = 全部 PASS，1 = 有 FAIL。
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${1:?output dir}"
ROUNDS="${2:-3}"
SCENARIO_LIST="${SCENARIOS:-head_on ring3 outside_wait}"
ROUND_TIMEOUT="${ROUND_TIMEOUT:-420}"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

set +u
# shellcheck disable=SC1091  # 运行时才有的工作区环境
source "${ROOT_DIR}/install/setup.bash"
set -u

# 清掉残留的 ROS 进程（方括号写法避免匹配到发起 ssh 的 shell 自己）。
sweep() {
  pkill -KILL -f "[r]obot_ws/install/" 2>/dev/null
  pkill -KILL -f "[r]os2 launch" 2>/dev/null
  pkill -KILL -f "[d]eadlock_scenarios.py" 2>/dev/null
  sleep 2
}

LAUNCH_PGID=""
stop_stack() {
  if [[ -n "${LAUNCH_PGID}" ]]; then
    kill -INT -- "-${LAUNCH_PGID}" 2>/dev/null; sleep 3
    kill -KILL -- "-${LAUNCH_PGID}" 2>/dev/null
    LAUNCH_PGID=""
  fi
  sweep
}
trap stop_stack EXIT

fails=0
for scenario in ${SCENARIO_LIST}; do
  stations="$(python3 "${SCRIPT_DIR}/deadlock_scenarios.py" --scenario "${scenario}" --print-start-stations)"
  robots="$(awk -F, '{ print NF }' <<<"${stations}")"
  for round in $(seq 1 "${ROUNDS}"); do
    tag="scenario_${scenario}_${round}"
    echo "[$(date +%T)] ${tag}: robots=${robots} start=${stations}"
    export ROS_DOMAIN_ID="$(( RANDOM % 150 + 50 ))"
    setsid ros2 launch robot_bringup fleet_sim.launch.py \
      fleet_size:="${robots}" network:=workshop_2way traffic_reservation_mode:=rolling \
      traffic_deadlock_resolution_enabled:=true start_stations:="${stations}" \
      > "${OUT_DIR}/${tag}.launch.log" 2>&1 < /dev/null &
    LAUNCH_PGID=$!

    # 等任务中枢的服务出现（ros2 CLI 带超时，DDS 卡住时不会挂死）。
    deadline=$((SECONDS + 90))
    until timeout 15 ros2 service type /v2/submit_order >/dev/null 2>&1; do
      if (( SECONDS >= deadline )); then
        echo "mission runner did not start" >> "${OUT_DIR}/${tag}.out"
        break
      fi
      sleep 2
    done

    timeout --kill-after=10 "${ROUND_TIMEOUT}" python3 "${SCRIPT_DIR}/deadlock_scenarios.py" \
      --scenario "${scenario}" --round "${round}" --out "${OUT_DIR}" \
      >> "${OUT_DIR}/${tag}.out" 2>&1 < /dev/null
    rc=$?
    if (( rc == 124 || rc == 137 )); then
      printf "%s\t%s\t-\t-\t-\t-\t-\t-\t-\tFAIL\ttimeout %ss\n" "${scenario}" "${round}" \
        "${ROUND_TIMEOUT}" >> "${OUT_DIR}/deadlock_scenarios.tsv"
    fi
    (( rc == 0 )) || fails=$((fails + 1))
    grep -E "traffic deadlock (detected|resolution|resolved|fallback)" \
      "${OUT_DIR}/${tag}.launch.log" > "${OUT_DIR}/${tag}.deadlock.log" 2>/dev/null
    stop_stack
    echo "[$(date +%T)] ${tag}: rc=${rc}"
  done
done

echo "deadlock scenarios: ${fails} failed round(s) -> ${OUT_DIR}/deadlock_scenarios.tsv"
(( fails == 0 ))
