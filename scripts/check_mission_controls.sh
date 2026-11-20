#!/usr/bin/env bash
# 任务控制：入队一份约 40 秒的长任务并开跑，依次暂停、恢复、取消。
# 默认的 demo_patrol 只有 2 个点、约 1 秒跑完，取消常常赶不上（诊断时两轮都偶然通过）。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

for svc in /pause_mission_profile /resume_mission_profile /cancel_mission_profile; do
  wait_service_type "${svc}" "std_srvs/srv/Trigger" 10 /tmp/robot_mission_controls_service.err
done
wait_service_type /v2/enqueue_mission_profile "robot_interfaces_mission/srv/EnqueueMission" 10 /tmp/robot_mission_controls_enqueue.err

mission_file="$(mktemp /tmp/robot_mission_controls.XXXXXX.yaml)"
trap 'rm -f "${mission_file}"' EXIT
write_long_mission "${mission_file}" controls_long 40

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/enqueue_mission_profile robot_interfaces_mission/srv/EnqueueMission \
  "{mission_file: '${mission_file}', priority: 50, start_if_idle: true}" | response_only)"
rg "success=True" <<<"${out}"
wait_active_mission controls_long 30 "RUNNING"

trigger() {
  local out
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "$1" std_srvs/srv/Trigger "{}" | response_only)"
  echo "${out}"
  rg "success=True" <<<"${out}"
}

trigger /pause_mission_profile
wait_active_mission controls_long 15 "PAUSED" seen
trigger /resume_mission_profile
wait_active_mission controls_long 15 "RUNNING" seen
trigger /cancel_mission_profile
wait_active_mission controls_long 15 "CANCELED" seen

echo "mission controls passed"
