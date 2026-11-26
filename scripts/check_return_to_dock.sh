#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
import os
from time import monotonic

import rclpy
from robot_interfaces.msg import RobotState
from std_srvs.srv import Trigger


def check(node):
    latest_state = None
    seen_dock = False

    def on_state(msg):
        nonlocal latest_state, seen_dock
        latest_state = msg
        seen_dock |= msg.active_goal_id == "dock_return"

    client = node.create_client(Trigger, "/return_to_dock")
    node.create_subscription(RobotState, "/mission_runner/state", on_state, 10)
    if not client.wait_for_service(timeout_sec=10):
        raise SystemExit("return-to-dock service not discovered")
    deadline = monotonic() + 10.0
    while latest_state is None and monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
    if latest_state is None:
        raise SystemExit("return-to-dock: no mission state before request")

    seen_dock = False
    result = client.call_async(Trigger.Request())
    rclpy.spin_until_future_complete(
        node, result, timeout_sec=float(os.environ.get("ROS2_CALL_TIMEOUT", "30"))
    )
    if not result.done():
        raise SystemExit(
            f"return-to-dock reply timed out; seen_dock={seen_dock}, state={latest_state.state}"
        )
    response = result.result()
    print(response)
    if not response.success:
        raise SystemExit("return-to-dock was rejected")

    deadline = monotonic() + 20.0
    while True:
        if seen_dock and latest_state.state == "FINISHED":
            print("return-to-dock mission finished")
            return
        if latest_state.state in {"FAILED", "ERROR"} or monotonic() >= deadline:
            raise SystemExit(
                f"return-to-dock did not finish; seen_dock={seen_dock}, state={latest_state.state}, "
                f"active_goal_id={latest_state.active_goal_id}"
            )
        rclpy.spin_once(node, timeout_sec=0.1)


rclpy.init()
node = rclpy.create_node("check_return_to_dock")
try:
    check(node)
finally:
    node.destroy_node()
    rclpy.shutdown()
PY
