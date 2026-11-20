#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_submit_order 10 /tmp/robot_scenario_sequence_cancel_submit.err
wait_service_type /v2/cancel_workflow "robot_interfaces_business/srv/CancelWorkflow" 10 /tmp/robot_scenario_sequence_cancel.err

_rg_out="$(call_submit_order campus_cancel_alpha scenario_workflow 0 \
  "scenario_id=campus;workflow_id=full_mail_route;order_id=campus_cancel_alpha;start_if_idle=false;preempt_current=false" \
  scenario_workflow | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_sequence_campus_cancel_alpha_step_1_leg_1" <<<"${_rg_out}"
rg "queue_size=4" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/cancel_workflow robot_interfaces_business/srv/CancelWorkflow \
  "{order_id: campus_cancel_alpha, cancel_active: false}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "station_sequence_campus_cancel_alpha_step_1_leg_1" <<<"${_rg_out}"
rg "station_sequence_campus_cancel_alpha_step_1_leg_4" <<<"${_rg_out}"
rg "queue_size=0" <<<"${_rg_out}"

echo "scenario station sequence cancel passed"
