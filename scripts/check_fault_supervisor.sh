#!/usr/bin/env bash
set -eo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

set +u
source install/setup.bash
set -u
# 本用例自起速度复用器。和外层 amr_demo 使用不同域，避免两套同名急停服务和状态话题互相干扰。
parent_domain="${ROS_DOMAIN_ID:-$((RANDOM % 150 + 50))}"
export ROS_DOMAIN_ID="$(( (10#${parent_domain} + 101) % 230 ))"

mux_log="/tmp/robot_fault_supervisor_mux.log"
supervisor_log="/tmp/robot_fault_supervisor.log"
ros2 run robot_teleop cmd_vel_mux_node >"${mux_log}" 2>&1 &
mux_pid=$!
ros2 run robot_utils fault_supervisor_node --ros-args -p startup_grace_ms:=0 -p command_cooldown_ms:=50 >"${supervisor_log}" 2>&1 &
supervisor_pid=$!

cleanup() {
  kill "${supervisor_pid}" "${mux_pid}" >/dev/null 2>&1 || true
  wait "${supervisor_pid}" "${mux_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

python3 - <<'PYTHON'
from time import monotonic

import rclpy
from rclpy.duration import Duration
from robot_interfaces.msg import RobotState
from std_msgs.msg import Bool
from std_srvs.srv import SetBool


def check(node):
    observed = {}
    health = node.create_publisher(RobotState, "/system_health", 10)
    node.create_subscription(Bool, "/emergency_stop/state",
                             lambda msg: observed.update(estop=msg.data), 10)
    node.create_subscription(RobotState, "/fault_supervisor/state",
                             lambda msg: observed.update(supervisor=msg.state), 10)

    def wait_for(predicate, description, seconds):
        deadline = monotonic() + seconds
        while not predicate() and monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
        if not predicate():
            raise RuntimeError(f"{description}: {observed}")

    for name in ("/enable_emergency_stop", "/clear_emergency_stop"):
        if not node.create_client(SetBool, name).wait_for_service(timeout_sec=5):
            raise RuntimeError(f"service did not appear: {name}")
    wait_for(lambda: health.get_subscription_count() > 0 and
             observed.get("estop") is False and "supervisor" in observed,
             "fault supervisor and mux readiness", 10)

    def inject(state, message, recoverable, expected):
        health.publish(RobotState(state=state, message=message, recoverable=recoverable))
        if not health.wait_for_all_acked(Duration(seconds=2)):
            raise RuntimeError(f"system health {state} did not reach the supervisor")
        wait_for(lambda: observed.get("estop") is expected,
                 f"emergency stop did not become {expected}", 10)
        print(f"emergency stop observed: {expected}")

    inject("ERROR", "injected fault", False, True)
    inject("OK", "recovered", True, False)


rclpy.init()
node = rclpy.create_node("fault_supervisor_acceptance")
try:
    check(node)
finally:
    node.destroy_node()
    rclpy.shutdown()
PYTHON

echo "Fault supervisor acceptance OK; logs: ${supervisor_log}, ${mux_log}"
