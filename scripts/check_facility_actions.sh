#!/usr/bin/env bash
# 设施动作的执行后保持与释放（specs/facility-reservation）：
#   1. 开门 hold_after_action=true：完成后门保持 occupied，占用者为该任务；
#   2. 非占用者释放被拒，门仍 occupied；占用者释放后恢复 available；
#   3. 叫电梯 hold_after_action=false：完成后电梯即 available；
#   4. 去远处的门执行 hold_after_action=true 的动作，执行中取消：门恢复 available。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/execute_facility_action "robot_interfaces_facility/srv/ExecuteFacilityAction" 10 /tmp/robot_facility_action.err
wait_service_type /v2/list_facility_resources "robot_interfaces_facility/srv/ListFacilityResources" 10 /tmp/robot_facility_action_list.err
wait_service_type /v2/release_facility_resource "robot_interfaces_facility/srv/ReleaseFacilityResource" 10 /tmp/robot_facility_action_release.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_facility_action_state_topic.err

execute_action() {
  local request_id="$1" resource_id="$2" resource_type="$3" action="$4" priority="$5" hold="$6" out
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/execute_facility_action \
    robot_interfaces_facility/srv/ExecuteFacilityAction \
    "{request_id: ${request_id}, resource_id: ${resource_id}, resource_type: ${resource_type}, action: ${action}, priority: ${priority}, start_if_idle: true, preempt_current: false, hold_after_action: ${hold}}" | response_only)"
  echo "${out}"
  rg "success=True" <<<"${out}"
  rg "mission_id='facility_action_${request_id}'" <<<"${out}"
  rg "resource_id='${resource_id}'" <<<"${out}"
}

release() {
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/release_facility_resource \
    robot_interfaces_facility/srv/ReleaseFacilityResource "{resource_id: $1, holder_id: $2}"
}

# 1. 开门并保持
execute_action door_alpha receiving_door door open 70 true
wait_active_mission facility_action_door_alpha 60
expect_facility_resource door receiving_door occupied facility_action_door_alpha False

# 2. 非占用者释放被拒，占用者释放成功
out="$(release receiving_door someone_else)"
rg "success=False" <<<"${out}"
rg "held by facility_action_door_alpha" <<<"${out}"
expect_facility_resource door receiving_door occupied facility_action_door_alpha False
out="$(release receiving_door facility_action_door_alpha)"
rg "success=True" <<<"${out}"
rg "status='available'" <<<"${out}"
expect_facility_resource door receiving_door available "" True

# 3. 叫电梯，不保持
execute_action elevator_alpha freight_elevator elevator call 65 false
wait_active_mission facility_action_elevator_alpha 60
expect_facility_resource elevator freight_elevator available "" True

# 4. 车在 packing（电梯处），去 receiving 的门执行保持型动作，途中取消
execute_action door_cancel receiving_door door open 70 true
wait_active_mission facility_action_door_cancel 30 "RUNNING"
out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /cancel_mission_profile std_srvs/srv/Trigger "{}" | response_only)"
rg "success=True" <<<"${out}"
wait_active_mission facility_action_door_cancel 30 "CANCELED" seen
expect_facility_resource door receiving_door available "" True

echo "facility action workflow passed"
