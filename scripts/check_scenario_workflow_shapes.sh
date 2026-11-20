#!/usr/bin/env bash
# 行业场景工作流（acceptance-suite-rework design D2：industry_workflows + more_industry_workflows 换层）。
# 原来两个用例把 11 个行业模板逐个完整跑一遍（约 13 分钟），模板之间只有站点不同、执行路径相同。
# 现在：
#   1. 场景列表里 11 个模板都在（原两个用例的列表断言，拆开逐个检查）；
#   2. 11 个模板逐个下单受理（start_if_idle=false），断言受理与第 1 步任务号，再取消排队、确认队列清空；
#   3. 两种执行形态各完整跑一个：两步都是 fleet_station（manufacturing/line_replenishment_loop），
#      fleet_station 加 station_sequence（restaurant/service_and_dish_return）。
# "每个模板的每一步都有有效路径"由 GoogleTest robot_tasks/test/industry_scenarios_test.cpp 校验。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/list_scenario_workflows "robot_interfaces_business/srv/ListScenarioWorkflows" 10 /tmp/robot_shapes_list.err
wait_service_type /v2/cancel_workflow "robot_interfaces_business/srv/CancelWorkflow" 10 /tmp/robot_shapes_cancel.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_shapes_events.err
wait_submit_order 10 /tmp/robot_shapes_submit.err

# 场景号 工作流号
TEMPLATES=(
  "manufacturing line_replenishment_loop"
  "hospital pharmacy_lab_round"
  "office workplace_services_round"
  "hotel guest_amenity_round"
  "laboratory sample_processing_round"
  "campus mail_distribution_round"
  "retail store_restock_and_recovery"
  "restaurant service_and_dish_return"
  "library returns_and_floor_round"
  "airport security_and_gate_supply"
  "factory_qc sample_and_rework_loop"
)

# 1. 场景列表
listing="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_scenario_workflows \
  robot_interfaces_business/srv/ListScenarioWorkflows "{scenario_id: ''}" | response_only)"
expect_eq "list success" "$(resp_scalar success <<<"${listing}")" True
for entry in "${TEMPLATES[@]}"; do
  read -r scenario workflow <<<"${entry}"
  rg -q "'${scenario}'" <<<"${listing}" || { echo "scenario ${scenario} not listed" >&2; exit 1; }
  rg -q "'${workflow}'" <<<"${listing}" || { echo "workflow ${workflow} not listed" >&2; exit 1; }
done
echo "all ${#TEMPLATES[@]} industry templates listed"

submit() {
  local scenario="$1" workflow="$2" order="$3" start="$4"
  call_submit_order "${order}" scenario_workflow 0 \
    "scenario_id=${scenario};workflow_id=${workflow};order_id=${order};start_if_idle=${start};preempt_current=false" \
    scenario_workflow
}

# 2. 逐个受理，再取消排队
for entry in "${TEMPLATES[@]}"; do
  read -r scenario workflow <<<"${entry}"
  order="accept_${scenario}"
  out="$(submit "${scenario}" "${workflow}" "${order}" false)"
  expect_eq "${scenario}/${workflow} accepted" "$(resp_scalar accepted <<<"${out}")" True
  rg -q "${order}_step_1" <<<"${out}" || { echo "${scenario}/${workflow}: step 1 mission missing: ${out}" >&2; exit 1; }
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/cancel_workflow \
    robot_interfaces_business/srv/CancelWorkflow "{order_id: ${order}, cancel_active: false}" | response_only)"
  expect_eq "${scenario}/${workflow} cancel success" "$(resp_scalar success <<<"${out}")" True
  expect_eq "${scenario}/${workflow} queue_size after cancel" "$(resp_scalar queue_size <<<"${out}")" 0
done

# 3. 两种执行形态各完整跑一个
start_station_auto_confirm

out="$(submit manufacturing line_replenishment_loop shape_fleet true)"
expect_eq "shape fleet+fleet accepted" "$(resp_scalar accepted <<<"${out}")" True
wait_mission_event_finished station_order_fleet_shape_fleet_step_1 120
wait_mission_event_finished station_order_fleet_shape_fleet_step_2 120

out="$(submit restaurant service_and_dish_return shape_sequence true)"
expect_eq "shape fleet+sequence accepted" "$(resp_scalar accepted <<<"${out}")" True
wait_mission_event_finished station_order_fleet_shape_sequence_step_1 120
wait_mission_event_finished station_sequence_shape_sequence_step_2_leg_2 120

echo "scenario workflow shapes passed"
