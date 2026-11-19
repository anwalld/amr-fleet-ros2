#!/usr/bin/env bash
# 死锁兜底暂停与解冻（acceptance-suite-rework specs：死锁兜底暂停与解冻验收；M3）。
# warehouse 站点没有避让点；再封掉 receiving–storage_b、storage_b–packing 这条绕行路线，
# 两车对向互等时谁都改不了道、也退不到避让点，第一次检出即兜底暂停。
#   1. /traffic/yield 发出 action=pause，cycle_robot_ids 含两车；
#   2. /traffic/state 的 pause_count 为 1；
#   3. 被冻结的车再申请被拒，原因写明已暂停；
#   4. 清除其中一辆车占用的资源后整组解冻，另一辆车再申请不再被拒。
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_traffic_self_common.sh"

# 关闭路口锁：两车可各占一条通向 storage_a 的路段（与 safety:traffic_deadlock 相同的构造）；开启解除。
start_mission_runner traffic_intersection_locks_enabled:=false traffic_deadlock_resolution_enabled:=true

for leg in "receiving storage_b" "storage_b packing"; do
  read -r a b <<<"${leg}"
  out="$(svc /v2/block_station_route robot_interfaces_mission/srv/BlockStationRoute "{from_station: ${a}, to_station: ${b}, reason: pause_check}")"
  expect_eq "block ${a}-${b}" "$(resp_scalar success <<<"${out}")" True
done

YIELD="${LOG_DIR}/yield.txt"
timeout 40 ros2 topic echo /traffic/yield robot_interfaces_mission/msg/TrafficYield > "${YIELD}" 2>/dev/null &
PIDS+=("$!")
sleep 3

out="$(request_segment robot_alpha receiving storage_a)"; expect_eq "alpha leg granted" "$(resp_scalar granted <<<"${out}")" True
out="$(request_segment robot_beta packing storage_a)"; expect_eq "beta leg granted" "$(resp_scalar granted <<<"${out}")" True
out="$(request_segment robot_alpha storage_a packing packing)"; expect_eq "alpha waits" "$(resp_scalar queued <<<"${out}")" True
request_segment robot_beta storage_a receiving receiving >/dev/null

# 1. pause 指令
deadline=$((SECONDS + 15))
until python3 - "${YIELD}" <<'PY'
import sys
msgs = open(sys.argv[1]).read().split("---")
ok = any("action: pause" in m and "robot_alpha" in m and "robot_beta" in m for m in msgs)
sys.exit(0 if ok else 1)
PY
do
  if (( SECONDS >= deadline )); then
    echo "no pause yield with both robots; got:" >&2
    cat "${YIELD}" >&2
    exit 1
  fi
  sleep 1
done
echo "ok: pause yield published for robot_alpha and robot_beta"

# 2. pause_count
state="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /traffic/state robot_interfaces_mission/msg/TrafficState --field pause_count)"
expect_eq "pause_count" "$(head -1 <<<"${state}")" 1

# 3. 冻结的车申请被拒
out="$(request_segment robot_alpha storage_a packing packing)"; echo "${out}"
expect_eq "frozen alpha granted" "$(resp_scalar granted <<<"${out}")" False
rg -q "paused by deadlock fallback" <<<"${out}"

# 4. 清除 alpha 占用的路段后整组解冻
out="$(svc /v2/clear_traffic_reservation robot_interfaces_mission/srv/ClearTrafficReservation "{resource_id: 'route_edge:receiving__storage_a'}")"
echo "${out}"
expect_eq "clear success" "$(resp_scalar success <<<"${out}")" True
expect_eq "clear owner" "$(resp_scalar owner_id <<<"${out}")" robot_alpha
out="$(request_segment robot_beta storage_a receiving receiving)"; echo "${out}"
if rg -q "paused by deadlock fallback" <<<"${out}"; then
  echo "robot_beta still frozen after clearing robot_alpha's reservation" >&2
  exit 1
fi
expect_eq "beta after unfreeze granted" "$(resp_scalar granted <<<"${out}")" True

echo "traffic deadlock pause and clear passed"
