#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_submit_order 10 /tmp/robot_business_order_submit.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_business_order_events.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_business_order_state_topic.err

_rg_out="$(call_submit_order biz_alpha business 0 \
  "business_order_id=biz_alpha;business_type=campus_mail;start_if_idle=true;preempt_current=false" \
  business_order)"
rg "accepted=True" <<<"${_rg_out}"
rg "campus" <<<"${_rg_out}"
rg "full_mail_route" <<<"${_rg_out}"
rg "station_sequence_business_biz_alpha_step_1_leg_1" <<<"${_rg_out}"

for mission in \
  station_sequence_business_biz_alpha_step_1_leg_1 \
  station_sequence_business_biz_alpha_step_1_leg_2 \
  station_sequence_business_biz_alpha_step_1_leg_3 \
  station_sequence_business_biz_alpha_step_1_leg_4; do
  deadline=$((SECONDS + 45))
  while true; do
    if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
      "{limit: 80, state_filter: FINISHED, mission_id_filter: ${mission}}" \
      2>/tmp/robot_business_order_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
      break
    fi
    if (( SECONDS >= deadline )); then
      echo "business order mission did not finish: ${mission}" >&2
      exit 1
    fi
    sleep 1
  done
done

echo "business order passed"
