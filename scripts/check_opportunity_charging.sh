#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/request_opportunity_charging "robot_interfaces_mission/srv/RequestOpportunityCharging" 10 /tmp/robot_opportunity_charge_service.err
wait_service_type /v2/list_facility_resources "robot_interfaces_facility/srv/ListFacilityResources" 10 /tmp/robot_opportunity_charge_list.err
wait_service_type /v2/release_facility_resource "robot_interfaces_facility/srv/ReleaseFacilityResource" 10 /tmp/robot_opportunity_charge_release.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_opportunity_charge_state_topic.err

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_opportunity_charging robot_interfaces_mission/srv/RequestOpportunityCharging \
  "{request_id: high_battery_skip, charger_id: charger_main, battery_voltage: 24.5, opportunity_threshold_voltage: 23.2, critical_threshold_voltage: 22.0, priority: 0, start_if_idle: true, preempt_current: false, require_idle_queue: true}")"
rg "success=True" <<<"${out}"
rg "charging_queued=False" <<<"${out}"
rg "battery above threshold" <<<"${out}"

expect_facility_resource charger charger_main available "" True

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/request_opportunity_charging robot_interfaces_mission/srv/RequestOpportunityCharging \
  "{request_id: opp_alpha, charger_id: charger_main, battery_voltage: 22.8, opportunity_threshold_voltage: 23.2, critical_threshold_voltage: 22.0, priority: 0, start_if_idle: true, preempt_current: false, require_idle_queue: true}")"
rg "success=True" <<<"${out}"
rg "charging_queued=True" <<<"${out}"
rg "mission_id='charging_request_opp_alpha'" <<<"${out}"

wait_active_mission charging_request_opp_alpha 60
expect_facility_resource charger charger_main occupied charging_request_opp_alpha False

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/release_facility_resource robot_interfaces_facility/srv/ReleaseFacilityResource \
  "{resource_id: charger_main, holder_id: charging_request_opp_alpha}")"
rg "success=True" <<<"${out}"
expect_facility_resource charger charger_main available "" True

echo "opportunity charging workflow passed"
