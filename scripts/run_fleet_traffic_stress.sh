#!/usr/bin/env bash
# 多机路权压测（M2 起；M3 加死锁指标列）。
# 用法：scripts/run_fleet_traffic_stress.sh <车数> <分钟> <模式 rolling|whole_route> [输出目录]
# 环境变量（M3）：
#   NETWORK=workshop|workshop_2way|warehouse   路网（默认 workshop，M2 的数字在它上面）
#   DEADLOCK_RESOLUTION=true|false              死锁解除开关（默认 true；false 只检测不解除）
# 拉起 fleet_sim.launch.py（任务中枢 + 无 Nav2 的 navigate_sequence_server + N 个运动学代理，
# 不开 Gazebo），再运行 fleet_traffic_stress.py 随机下单、每秒记录、判定违规、采死锁指标。
# 结果：<输出目录>/{state.csv,tasks.csv,violations.csv,deadlocks.csv,summary.json,launch.log}
# 退出码：0 = 无违规，1 = 有违规，2 = 车队没就绪。
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROBOTS="${1:?robots}"
MINUTES="${2:?minutes}"
MODE="${3:?mode}"
NETWORK="${NETWORK:-workshop}"
DEADLOCK_RESOLUTION="${DEADLOCK_RESOLUTION:-true}"
OUT_DIR="${4:-/tmp/fleet_stress_${ROBOTS}_${MODE}_${NETWORK}_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "${OUT_DIR}"
echo "network=${NETWORK} deadlock_resolution=${DEADLOCK_RESOLUTION}" > "${OUT_DIR}/config.txt"

set +u
# shellcheck disable=SC1091  # 运行时才有的工作区环境
source "${ROOT_DIR}/install/setup.bash"
set -u
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 150 + 50))}"

setsid ros2 launch robot_bringup fleet_sim.launch.py \
  fleet_size:="${ROBOTS}" network:="${NETWORK}" traffic_reservation_mode:="${MODE}" \
  traffic_deadlock_resolution_enabled:="${DEADLOCK_RESOLUTION}" \
  >"${OUT_DIR}/launch.log" 2>&1 < /dev/null &
LAUNCH_PGID=$!
# shellcheck disable=SC2329  # 由 trap 调用
cleanup() {
  kill -INT -- "-${LAUNCH_PGID}" 2>/dev/null; sleep 3
  kill -KILL -- "-${LAUNCH_PGID}" 2>/dev/null
}
trap cleanup EXIT

# 等任务中枢的服务就绪（ros2 CLI 带超时，DDS 卡住时不会挂死）。
deadline=$((SECONDS + 90))
until timeout 15 ros2 service type /v2/submit_order >/dev/null 2>&1 && \
      timeout 15 ros2 service type /v2/request_route_segment >/dev/null 2>&1; do
  if (( SECONDS >= deadline )); then
    echo "mission runner did not start" >&2
    exit 2
  fi
  sleep 2
done

python3 "${SCRIPT_DIR}/fleet_traffic_stress.py" \
  --robots "${ROBOTS}" --minutes "${MINUTES}" --mode "${MODE}" --out "${OUT_DIR}" \
  --network "${NETWORK}" --deadlock-resolution "${DEADLOCK_RESOLUTION}" \
  2>&1 | tee "${OUT_DIR}/stress.log"
rc=${PIPESTATUS[0]}
uptime >>"${OUT_DIR}/stress.log"
echo "fleet traffic stress (${ROBOTS} robots, ${MINUTES} min, ${MODE}, ${NETWORK}) rc=${rc} -> ${OUT_DIR}"
exit "${rc}"
