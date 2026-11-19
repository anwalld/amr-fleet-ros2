#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/list_facility_resources "robot_interfaces_facility/srv/ListFacilityResources" 10 /tmp/robot_facility_list.err
wait_service_type /v2/reserve_facility_resource "robot_interfaces_facility/srv/ReserveFacilityResource" 10 /tmp/robot_facility_reserve.err
wait_service_type /v2/release_facility_resource "robot_interfaces_facility/srv/ReleaseFacilityResource" 10 /tmp/robot_facility_release.err
wait_service_type /v2/request_charging "robot_interfaces_mission/srv/RequestCharging" 10 /tmp/robot_charging_request.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_charging_state_topic.err

expect_facility_resource charger charger_main available "" True

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/reserve_facility_resource robot_interfaces_facility/srv/ReserveFacilityResource \
  "{resource_id: receiving_door, holder_id: operator_a, mission_id: manual_door_check, release_existing_for_holder: true}")"
rg "success=True" <<<"${out}"
rg "holder_id='operator_a'" <<<"${out}"
rg "status='reserved'" <<<"${out}"

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/release_facility_resource robot_interfaces_facility/srv/ReleaseFacilityResource \
  "{resource_id: receiving_door, holder_id: operator_a}")"
rg "success=True" <<<"${out}"
rg "status='available'" <<<"${out}"

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_charging robot_interfaces_mission/srv/RequestCharging \
  "{request_id: charge_alpha, charger_id: charger_main, priority: 80, start_if_idle: true, preempt_current: false}")"
rg "success=True" <<<"${out}"
rg "mission_id='charging_request_charge_alpha'" <<<"${out}"
rg "charger_id='charger_main'" <<<"${out}"

wait_active_mission charging_request_charge_alpha 60
# 充电预约默认执行后保持（specs/facility-reservation）：完成后充电桩仍被该任务占用。
expect_facility_resource charger charger_main occupied charging_request_charge_alpha False

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/release_facility_resource robot_interfaces_facility/srv/ReleaseFacilityResource \
  "{resource_id: charger_main, holder_id: charging_request_charge_alpha}")"
rg "success=True" <<<"${out}"
expect_facility_resource charger charger_main available "" True

echo "facility resource reservation and charging request workflow passed"
