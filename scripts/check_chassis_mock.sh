#!/usr/bin/env bash
# 底盘与里程计轨迹（acceptance-suite-rework design D8 补覆盖）。mock_bringup 栈。
#   /v2/set_chassis_mode：mecanum 生效；ackermann 受理但说明未实现、回落 diff_drive；未知模式被拒；
#   最后恢复 diff_drive。/clear_odometry、/reset_odom_path（std_srvs/Empty）都能调用成功。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/set_chassis_mode "robot_interfaces_core/srv/SetChassisMode" 20 /tmp/robot_chassis_mode.err
wait_service_type /clear_odometry "std_srvs/srv/Empty" 20 /tmp/robot_chassis_clear.err
wait_service_type /reset_odom_path "std_srvs/srv/Empty" 20 /tmp/robot_chassis_reset_path.err

set_mode() {
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/set_chassis_mode robot_interfaces_core/srv/SetChassisMode "{mode: $1}"
}

out="$(set_mode mecanum)"
expect_eq "mecanum success" "$(resp_scalar success <<<"${out}")" True
rg -q "kinematics=mecanum" <<<"${out}"
out="$(set_mode ackermann)"
expect_eq "ackermann success" "$(resp_scalar success <<<"${out}")" True
rg -q "is not implemented; using diff_drive kinematics" <<<"${out}"
out="$(set_mode bogus_mode)"
expect_eq "unknown mode success" "$(resp_scalar success <<<"${out}")" False
out="$(set_mode diff_drive)"
expect_eq "restore diff_drive" "$(resp_scalar success <<<"${out}")" True
rg -q "kinematics=diff_drive" <<<"${out}"

for svc in /clear_odometry /reset_odom_path; do
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "${svc}" std_srvs/srv/Empty "{}")"
  rg -q "Empty_Response" <<<"${out}" || { echo "${svc} call failed: ${out}" >&2; exit 1; }
  echo "ok: ${svc}"
done

echo "chassis mock passed"
