#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_submit_order 10 /tmp/robot_workflow_pause_submit.err
wait_service_type /v2/pause_workflow "robot_interfaces_business/srv/PauseWorkflow" 10 /tmp/robot_workflow_pause.err
wait_service_type /v2/resume_workflow "robot_interfaces_business/srv/ResumeWorkflow" 10 /tmp/robot_workflow_resume.err
wait_service_type /v2/get_workflow_status "robot_interfaces_business/srv/GetWorkflowStatus" 10 /tmp/robot_workflow_status.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_workflow_pause_events.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_workflow_pause_state_topic.err

# 站点运输要等装卸确认，后台自动发（见 robot_wait.sh）。
start_station_auto_confirm

_rg_out="$(call_submit_order pause_alpha scenario_workflow 30 \
  "scenario_id=warehouse;workflow_id=inbound_crossdock;order_id=pause_alpha;start_if_idle=false;preempt_current=false" \
  scenario_workflow)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_1" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_2" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/pause_workflow robot_interfaces_business/srv/PauseWorkflow \
  "{order_id: pause_alpha, pause_active: true}")"
rg "success=True" <<<"${_rg_out}"
rg "active_pause_requested=False" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_1" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_2" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_workflow_status robot_interfaces_business/srv/GetWorkflowStatus \
  "{order_id: pause_alpha}")"
rg "success=True" <<<"${_rg_out}"
rg "state='PAUSED'" <<<"${_rg_out}"
rg "paused=True" <<<"${_rg_out}"
rg "queued_steps=2" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_2" <<<"${_rg_out}"

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/resume_workflow robot_interfaces_business/srv/ResumeWorkflow \
  "{order_id: pause_alpha, resume_active: true}")"
rg "success=True" <<<"${_rg_out}"
rg "active_resume_requested=False" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_1" <<<"${_rg_out}"
rg "station_order_fleet_pause_alpha_step_2" <<<"${_rg_out}"

for mission in station_order_fleet_pause_alpha_step_1 station_order_fleet_pause_alpha_step_2; do
  deadline=$((SECONDS + 90))
  while true; do
    if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
      "{limit: 80, state_filter: FINISHED, mission_id_filter: ${mission}}" \
      2>/tmp/robot_workflow_pause_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
      break
    fi
    if (( SECONDS >= deadline )); then
      echo "workflow mission did not finish: ${mission}" >&2
      exit 1
    fi
    sleep 1
  done
done

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_workflow_status robot_interfaces_business/srv/GetWorkflowStatus \
  "{order_id: pause_alpha}")"
rg "success=True" <<<"${_rg_out}"
rg "state='FINISHED'" <<<"${_rg_out}"
rg "finished_steps=2" <<<"${_rg_out}"
rg "total_steps=2" <<<"${_rg_out}"

echo "workflow pause resume passed"
