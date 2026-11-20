#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/get_operator_snapshot "robot_interfaces_business/srv/GetOperatorSnapshot" 10 /tmp/robot_operator_snapshot.err
wait_submit_order 10 /tmp/robot_operator_snapshot_submit.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_operator_snapshot_events.err

# 站点运输要等装卸确认，后台自动发（见 robot_wait.sh）。
start_station_auto_confirm

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_operator_snapshot robot_interfaces_business/srv/GetOperatorSnapshot "{event_limit: 5}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "operator snapshot" <<<"${_rg_out}"
rg "fleet_robot_ids=\\[|facility_resource_ids=\\[|business_types=\\[" <<<"${_rg_out}"

_rg_out="$(call_submit_order operator_alpha business 0 \
  "business_order_id=operator_alpha;business_type=warehouse_inbound;start_if_idle=true;preempt_current=false" \
  business_order | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "warehouse" <<<"${_rg_out}"
rg "door_assisted_inbound" <<<"${_rg_out}"

for mission in facility_action_business_operator_alpha_step_1 station_order_fleet_business_operator_alpha_step_2; do
  deadline=$((SECONDS + 90))
  while true; do
    if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
      "{limit: 80, state_filter: FINISHED, mission_id_filter: ${mission}}" \
      2>/tmp/robot_operator_snapshot_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
      break
    fi
    if (( SECONDS >= deadline )); then
      echo "operator snapshot mission did not finish: ${mission}" >&2
      exit 1
    fi
    sleep 1
  done
done

_rg_out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_operator_snapshot robot_interfaces_business/srv/GetOperatorSnapshot "{event_limit: 100}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "FINISHED" <<<"${_rg_out}"
rg "facility_action_business_operator_alpha_step_1" <<<"${_rg_out}"
rg "station_order_fleet_business_operator_alpha_step_2" <<<"${_rg_out}"
rg "business_types=\\[" <<<"${_rg_out}"

echo "operator snapshot passed"
