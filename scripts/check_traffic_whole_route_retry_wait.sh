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
# core 栈的模式、解除开关和 TTL 由运行器设置。
wait_service_type /v2/request_route_segment "robot_interfaces_mission/srv/RequestRouteSegment" 20
wait_service_type /v2/detect_traffic_deadlock "robot_interfaces_mission/srv/DetectTrafficDeadlock" 20

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
expect_eq "beta blocked queued" "$(resp_scalar queued <<<"${out}")" False
rg -q "robot_alpha" <<<"${out}"

out="$(detect)"; echo "${out}"
expect_eq "detect within ttl success" "$(resp_scalar success <<<"${out}")" True
expect_eq "deadlocked within ttl" "$(resp_scalar deadlocked <<<"${out}")" True
for robot in robot_alpha robot_beta; do
  expect_eq "detected mission_ids contains ${robot}" \
    "$(resp_row mission_ids "${robot}" mission_ids <<<"${out}")" "${robot}"
done

sleep $((TTL + 2))
out="$(detect)"; echo "${out}"
expect_eq "detect after ttl success" "$(resp_scalar success <<<"${out}")" True
expect_eq "deadlocked after ttl" "$(resp_scalar deadlocked <<<"${out}")" False

echo "whole_route retry wait passed"
