#!/usr/bin/env bash
# 导航序列服务的直接控制接口（acceptance-suite-rework design D8 补覆盖）。core 栈。
#   /pause_mission、/resume_mission、/cancel_mission（std_srvs/Trigger）与 /v2/get_statistics。
# 入队一份长任务开跑，经导航序列服务直接暂停 → 任务中枢显示 PAUSED；恢复 → RUNNING；
# 取消 → 任务中枢 CANCELED；统计里的目标数与成功数都大于 0。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

for svc in /pause_mission /resume_mission /cancel_mission; do
  wait_service_type "${svc}" "std_srvs/srv/Trigger" 20 /tmp/robot_nav_seq_ctl.err
done
wait_service_type /v2/get_statistics "robot_interfaces_core/srv/GetStatistics" 20 /tmp/robot_nav_seq_stats.err
wait_service_type /v2/enqueue_mission_profile "robot_interfaces_mission/srv/EnqueueMission" 20 /tmp/robot_nav_seq_enqueue.err

mission_file="$(mktemp /tmp/robot_nav_seq_controls.XXXXXX.yaml)"
trap 'rm -f "${mission_file}"' EXIT
write_long_mission "${mission_file}" nav_seq_controls 40

trigger() {
  local out
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "$1" std_srvs/srv/Trigger "{}" | response_only)"
  expect_eq "$1 success" "$(resp_scalar success <<<"${out}")" True
  expect_eq "$1 message" "$(resp_scalar message <<<"${out}")" "$2"
}

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/enqueue_mission_profile robot_interfaces_mission/srv/EnqueueMission \
  "{mission_file: '${mission_file}', priority: 50, start_if_idle: true}" | response_only)"
expect_eq "enqueue success" "$(resp_scalar success <<<"${out}")" True
wait_active_mission nav_seq_controls 30 "RUNNING"
sleep 3

trigger /pause_mission "mission paused"
wait_active_mission nav_seq_controls 15 "PAUSED" seen
trigger /resume_mission "mission resumed"
wait_active_mission nav_seq_controls 15 "RUNNING" seen
trigger /cancel_mission "mission cancellation requested"
wait_active_mission nav_seq_controls 30 "CANCELED" seen

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/get_statistics robot_interfaces_core/srv/GetStatistics "{}" | response_only)"
echo "${out}"
total="$(resp_scalar total_goals <<<"${out}")"
succeeded="$(resp_scalar succeeded_goals <<<"${out}")"
(( total > 0 && succeeded > 0 )) || { echo "statistics: total_goals=${total} succeeded_goals=${succeeded}" >&2; exit 1; }
echo "ok: statistics total_goals=${total} succeeded_goals=${succeeded}"

echo "navigate sequence controls passed"
