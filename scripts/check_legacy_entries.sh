#!/usr/bin/env bash
# 旧提交入口兼容（acceptance-suite-rework design D8）：4 个旧入口现在都经统一入口 /v2/submit_order 转发，
# 仓库里不再直接调用（lint 见 ops:legacy_usage，本脚本是它唯一的白名单）；这里各调一次，确认对外仍兼容。
# core 栈，warehouse 站点；都不开跑（start_if_idle=false），最后清空队列。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

svc() { timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "$@"; }
wait_service_type /v2/submit_station_transport_order "robot_interfaces_mission/srv/SubmitStationTransportOrder" 20 /tmp/robot_legacy_1.err
wait_service_type /v2/submit_station_sequence_task "robot_interfaces_mission/srv/SubmitStationSequenceTask" 20 /tmp/robot_legacy_2.err
wait_service_type /v2/submit_station_order_batch "robot_interfaces_mission/srv/SubmitStationOrderBatch" 20 /tmp/robot_legacy_3.err
wait_service_type /v2/submit_fleet_station_task "robot_interfaces_fleet/srv/SubmitFleetStationTask" 20 /tmp/robot_legacy_4.err

out="$(svc /v2/submit_station_transport_order robot_interfaces_mission/srv/SubmitStationTransportOrder \
  "{order_id: legacy_transport, pickup_station: receiving, dropoff_station: storage_a, priority: 10, start_if_idle: false, preempt_current: false}")"
expect_eq "transport success" "$(resp_scalar success <<<"${out}")" True
rg -q "legacy_transport" <<<"$(resp_scalar mission_id <<<"${out}")"

out="$(svc /v2/submit_station_sequence_task robot_interfaces_mission/srv/SubmitStationSequenceTask \
  "{order_id: legacy_sequence, station_ids: [receiving, storage_a, packing], priority: 10, start_if_idle: false, preempt_current: false}")"
expect_eq "sequence success" "$(resp_scalar success <<<"${out}")" True
expect_eq "sequence queued_count" "$(resp_scalar queued_count <<<"${out}")" 2

out="$(svc /v2/submit_station_order_batch robot_interfaces_mission/srv/SubmitStationOrderBatch \
  "{batch_id: legacy_batch, order_ids: [legacy_b1, legacy_b2], pickup_station_ids: [receiving, storage_b], dropoff_station_ids: [storage_a, packing], priority: 10, start_if_idle: false, preempt_current: false, continue_on_error: true}")"
expect_eq "batch success" "$(resp_scalar success <<<"${out}")" True
expect_eq "batch accepted_count" "$(resp_scalar accepted_count <<<"${out}")" 2
expect_eq "batch rejected_count" "$(resp_scalar rejected_count <<<"${out}")" 0

out="$(svc /v2/submit_fleet_station_task robot_interfaces_fleet/srv/SubmitFleetStationTask \
  "{task_id: legacy_fleet, required_capability: station_transport, pickup_station: receiving, dropoff_station: storage_a, priority: 10, start_if_idle: false, preempt_current: false}")"
expect_eq "fleet success" "$(resp_scalar success <<<"${out}")" True
expect_eq "fleet task_id" "$(resp_scalar task_id <<<"${out}")" legacy_fleet

echo "legacy submit entries passed"
