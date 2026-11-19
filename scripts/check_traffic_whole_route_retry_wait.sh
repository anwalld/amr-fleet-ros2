#!/usr/bin/env bash
# whole_route 重试等待（acceptance-suite-rework specs：whole_route 重试等待验收；M3）。
# whole_route 模式下被拒的申请不排队，记为有效期 traffic_whole_route_wait_ttl_s 的"重试等待"并计入等待图。
#   1. alpha 拿下 receiving→storage_a，beta 拿下 packing→storage_b（整路申请，各自获授予）；
#   2. alpha 申请穿过 beta 占用部分的整条路径、beta 申请穿过 alpha 占用部分的整条路径，都被拒；
#   3. 有效期内检测：deadlocked=True，两车都在 mission_ids 中；
#   4. 不再重试、等过有效期后检测：deadlocked=False。
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_traffic_self_common.sh"

TTL=4
start_mission_runner traffic_reservation_mode:=whole_route traffic_deadlock_resolution_enabled:=false \
  traffic_whole_route_wait_ttl_s:="${TTL}.0"

out="$(request_whole_path robot_alpha receiving storage_a)"; echo "${out}"
expect_eq "alpha first leg granted" "$(resp_scalar granted <<<"${out}")" True
out="$(request_whole_path robot_beta packing storage_b)"; echo "${out}"
expect_eq "beta first leg granted" "$(resp_scalar granted <<<"${out}")" True

out="$(request_whole_path robot_alpha storage_a packing storage_b)"; echo "${out}"
expect_eq "alpha blocked granted" "$(resp_scalar granted <<<"${out}")" False
expect_eq "alpha blocked queued" "$(resp_scalar queued <<<"${out}")" False
rg -q "robot_beta" <<<"${out}"
out="$(request_whole_path robot_beta storage_b receiving storage_a)"; echo "${out}"
expect_eq "beta blocked granted" "$(resp_scalar granted <<<"${out}")" False
rg -q "robot_alpha" <<<"${out}"

out="$(detect)"; echo "${out}"
expect_eq "deadlocked within ttl" "$(resp_scalar deadlocked <<<"${out}")" True
rg -q "robot_alpha" <<<"${out}"
rg -q "robot_beta" <<<"${out}"

sleep $((TTL + 2))
out="$(detect)"; echo "${out}"
expect_eq "deadlocked after ttl" "$(resp_scalar deadlocked <<<"${out}")" False

echo "whole_route retry wait passed"
