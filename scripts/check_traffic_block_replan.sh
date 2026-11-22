#!/usr/bin/env bash
# 封路重规划（acceptance-suite-rework specs：封路重规划验收；M2 的封路行为）。core 栈，warehouse 站点。
#   1. robot_alpha 申请 receiving→storage_a，获授予（占路段 receiving–storage_a 与路口 storage_a）；
#   2. robot_beta 申请 packing→storage_a，因路口 storage_a 被占而排队；
#   3. 封住 beta 要走的路段 packing–storage_a：beta 被移出队列，/traffic/grants 收到 event=replan；
#   4. beta 再申请同一段：replan_required=True，既不授予也不排队；
#   5. 解封、alpha 释放后，beta 再申请获授予。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/request_route_segment "robot_interfaces_mission/srv/RequestRouteSegment" 20 /tmp/robot_block_replan_req.err
wait_service_type /v2/release_route_segment "robot_interfaces_mission/srv/ReleaseRouteSegment" 20 /tmp/robot_block_replan_rel.err
wait_service_type /v2/block_station_route "robot_interfaces_mission/srv/BlockStationRoute" 20 /tmp/robot_block_replan_block.err
wait_service_type /v2/unblock_station_route "robot_interfaces_mission/srv/UnblockStationRoute" 20 /tmp/robot_block_replan_unblock.err

GRANTS="$(mktemp /tmp/robot_block_replan_grants.XXXXXX)"
ECHO_PID=""
cleanup() {
  [[ -n "${ECHO_PID}" ]] && kill "${ECHO_PID}" >/dev/null 2>&1
  rm -f "${GRANTS}"
}
trap cleanup EXIT

# 1. alpha 获授予
out="$(call_request_route_segment robot_alpha receiving storage_a 10)"
echo "${out}"
expect_eq "alpha granted" "$(resp_scalar granted <<<"${out}")" True
rg -q "route_node:storage_a" <<<"${out}"

# 2. beta 排队
out="$(call_request_route_segment robot_beta packing storage_a 10)"
echo "${out}"
expect_eq "beta granted" "$(resp_scalar granted <<<"${out}")" False
expect_eq "beta queued" "$(resp_scalar queued <<<"${out}")" True
rg -q "robot_alpha" <<<"${out}"

# 3. 先开始收 /traffic/grants，再封 beta 要走的路段
timeout 40 ros2 topic echo /traffic/grants robot_interfaces_mission/msg/TrafficGrant > "${GRANTS}" 2>/dev/null &
ECHO_PID=$!
sleep 3
out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/block_station_route robot_interfaces_mission/srv/BlockStationRoute \
  "{from_station: packing, to_station: storage_a, reason: block_replan_check}" | response_only)"
echo "${out}"
expect_eq "block success" "$(resp_scalar success <<<"${out}")" True
expect_eq "block resource_id" "$(resp_scalar resource_id <<<"${out}")" "route_edge:packing__storage_a"
deadline=$((SECONDS + 15))
until python3 - "${GRANTS}" <<'PY'
import sys
msgs = open(sys.argv[1]).read().split("---")
sys.exit(0 if any("event: replan" in m and "robot_id: robot_beta" in m for m in msgs) else 1)
PY
do
  if (( SECONDS >= deadline )); then
    echo "no replan notice for robot_beta on /traffic/grants; got:" >&2
    cat "${GRANTS}" >&2
    exit 1
  fi
  sleep 1
done
echo "ok: robot_beta received replan notice"

# 4. 撞上封路：replan_required
out="$(call_request_route_segment robot_beta packing storage_a 10)"
echo "${out}"
expect_eq "beta replan_required" "$(resp_scalar replan_required <<<"${out}")" True
expect_eq "beta granted while blocked" "$(resp_scalar granted <<<"${out}")" False
expect_eq "beta queued while blocked" "$(resp_scalar queued <<<"${out}")" False

# 5. 解封、alpha 释放后 beta 获授予
out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/unblock_station_route robot_interfaces_mission/srv/UnblockStationRoute \
  "{from_station: packing, to_station: storage_a}" | response_only)"
expect_eq "unblock success" "$(resp_scalar success <<<"${out}")" True
out="$(call_release_route_segment robot_alpha)"
expect_eq "alpha release success" "$(resp_scalar success <<<"${out}")" True
out="$(call_request_route_segment robot_beta packing storage_a 10)"
echo "${out}"
expect_eq "beta granted after unblock" "$(resp_scalar granted <<<"${out}")" True

echo "traffic block replan passed"
