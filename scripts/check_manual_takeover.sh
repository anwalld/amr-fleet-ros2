#!/usr/bin/env bash
# 人工接管（acceptance-suite-rework design D8 补覆盖）。mock_bringup 栈（virtual_rc_node + cmd_vel_safety_gate_node）。
#   /rc/joy 在发布；/set_manual_takeover true 后 /manual_takeover/state、/cmd_vel_mux/manual_takeover、
#   /cmd_vel_mux/manual_takeover_lock 都为 true；设回 false 后都为 false。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /set_manual_takeover "std_srvs/srv/SetBool" 20 /tmp/robot_takeover.err
timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /rc/joy sensor_msgs/msg/Joy >/dev/null
echo "ok: /rc/joy publishing"

expect_bool_topic() {
  local topic="$1" want="$2" deadline=$((SECONDS + 10)) got=""
  while (( SECONDS < deadline )); do
    got="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once "${topic}" std_msgs/msg/Bool --field data 2>/dev/null | head -1 || true)"
    [[ "${got}" == "${want}" ]] && { echo "ok: ${topic} = ${want}"; return 0; }
    sleep 0.5
  done
  echo "${topic}: got '${got}', want '${want}'" >&2
  return 1
}

for value in true false; do
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /set_manual_takeover std_srvs/srv/SetBool "{data: ${value}}")"
  expect_eq "set_manual_takeover ${value}" "$(resp_scalar success <<<"${out}")" True
  for topic in /manual_takeover/state /cmd_vel_mux/manual_takeover /cmd_vel_mux/manual_takeover_lock; do
    expect_bool_topic "${topic}" "${value}"
  done
done

echo "manual takeover passed"
