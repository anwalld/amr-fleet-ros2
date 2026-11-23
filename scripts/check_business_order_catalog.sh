#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/list_business_order_types "robot_interfaces_business/srv/ListBusinessOrderTypes" 10 /tmp/robot_business_catalog_list.err
wait_submit_order 10 /tmp/robot_business_catalog_submit.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_business_catalog_events.err

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_business_order_types robot_interfaces_business/srv/ListBusinessOrderTypes "{}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "warehouse_inbound" <<<"${_rg_out}"
rg "campus_mail" <<<"${_rg_out}"
rg "hospital_round" <<<"${_rg_out}"
rg "laboratory_samples" <<<"${_rg_out}"

_rg_out="$(call_submit_order biz_catalog_alpha business 0 \
  "business_order_id=biz_catalog_alpha;business_type=campus_mail;start_if_idle=true;preempt_current=false" \
  business_order | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "campus" <<<"${_rg_out}"
rg "full_mail_route" <<<"${_rg_out}"
rg "station_sequence_business_biz_catalog_alpha_step_1_leg_1" <<<"${_rg_out}"

for mission in \
  station_sequence_business_biz_catalog_alpha_step_1_leg_1 \
  station_sequence_business_biz_catalog_alpha_step_1_leg_2 \
  station_sequence_business_biz_catalog_alpha_step_1_leg_3 \
  station_sequence_business_biz_catalog_alpha_step_1_leg_4; do
  deadline=$((SECONDS + 45))
  while true; do
    if service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
      "{limit: 80, state_filter: FINISHED, mission_id_filter: ${mission}}" \
      2>/tmp/robot_business_catalog_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
      break
    fi
    if (( SECONDS >= deadline )); then
      echo "business catalog mission did not finish: ${mission}" >&2
      exit 1
    fi
    sleep 1
  done
done

echo "business order catalog passed"
