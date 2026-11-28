"""Verify stop motion and explicit queue release with an independent odometry plant."""

import math
import pathlib
import sys
import time

import rclpy
from action_msgs.msg import GoalStatusArray
from geometry_msgs.msg import PoseWithCovarianceStamped, Twist
from nav_msgs.msg import Odometry
from robot_interfaces.msg import LocalizationHealth, RobotState
from robot_interfaces_mission.srv import EnqueueMission, ReleaseMissionDispatchHold
from std_srvs.srv import Trigger

from task_control_trace import TaskControlTrace


def run():
    trace = TaskControlTrace()
    node = rclpy.create_node("motion_cutover_probe")
    odometry = Odometry()
    odometry.pose.pose.orientation.w = 1.0
    position = odometry.pose.pose.position
    command = Twist()
    localized = False
    runner_state = ""
    goal_ids = set()
    stamps = []

    def note_command(message):
        nonlocal command
        command = message
        trace.message("velocity_command", message)

    def note_state(message):
        nonlocal runner_state
        if message.state != runner_state:
            trace.message("mission_state", message)
        runner_state = message.state

    def note_localization(message):
        nonlocal localized
        localized = message.localized
        trace.message("localization", message)

    def note_goals(message):
        goal_ids.update(tuple(goal.goal_info.goal_id.uuid) for goal in message.status_list)
        trace.message("navigation_goals", message)

    node.create_subscription(Twist, "/virtual_rc/cmd_vel", note_command, 10)
    node.create_subscription(RobotState, "/mission_runner/state", note_state, 10)
    node.create_subscription(LocalizationHealth, "/localization_health", note_localization, 10)
    node.create_subscription(GoalStatusArray, "/navigate_sequence/_action/status", note_goals, 10)
    odometry_pub = node.create_publisher(Odometry, "/model/mobile_robot/odometry", 10)
    localization_pub = node.create_publisher(PoseWithCovarianceStamped, "/amcl_pose", 10)

    def step():
        position.x += command.linear.x * 0.05
        position.y += command.linear.y * 0.05
        odometry.header.stamp = node.get_clock().now().to_msg()
        odometry_pub.publish(odometry)
        stamps.append(time.monotonic())
        trace.message("odometry", odometry)

    node.create_timer(0.05, step)

    def until(check, seconds=7):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
            if check():
                return
        raise AssertionError("condition timed out", runner_state, goal_ids)

    def call(name, service=Trigger, request=None, expected=True):
        client = node.create_client(service, "/" + name)
        try:
            assert client.wait_for_service(timeout_sec=8), name
            request = request or service.Request()
            trace.message("service_request", request, service=name)
            future = client.call_async(request)
            rclpy.spin_until_future_complete(node, future, timeout_sec=5)
            assert future.done() and future.result() is not None, name
            reply = future.result()
            trace.message("service_reply", reply, service=name)
            assert reply.success == expected, (name, reply)
            return reply
        finally:
            node.destroy_client(client)

    try:
        until(lambda: localization_pub.get_subscription_count() > 0)
        sample = PoseWithCovarianceStamped()
        sample.pose.pose.orientation.w = 1.0
        localization_pub.publish(sample)
        until(lambda: localized)
        call("start_mission_profile")
        until(lambda: position.x > 0.05 and len(goal_ids) == 1)
        old_goals = goal_ids.copy()
        call("v2/enqueue_mission_profile", EnqueueMission, EnqueueMission.Request(
            mission_file=str(pathlib.Path(sys.argv[1]).resolve()), priority=40, start_if_idle=True))
        call("pause_mission_profile")
        until(lambda: runner_state == "PAUSED" and abs(command.linear.x) < 1e-6)
        trace.record("stationary_window_start")
        origin = position.x, position.y
        stamps.clear()
        deadline = time.monotonic() + 1.2
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
            assert math.dist(origin, (position.x, position.y)) <= 0.01
            assert abs(command.angular.z) < 1e-6
        assert len(stamps) > 5 and stamps[-1] - stamps[0] >= 1.0
        assert max(end - start for start, end in zip(stamps, stamps[1:])) <= 0.2
        trace.record("stationary_window_end")
        call("cancel_mission_profile")
        until(lambda: runner_state == "CANCELED")
        hold_service = "v2/release_mission_dispatch_hold"
        hold = call(hold_service, ReleaseMissionDispatchHold)
        assert hold.held and hold.candidate_mission_id == "d16_motion_next", hold
        call("start_mission_profile", expected=False)
        assert goal_ids == old_goals, goal_ids
        call(hold_service, ReleaseMissionDispatchHold, ReleaseMissionDispatchHold.Request(
            mission_id=hold.candidate_mission_id, hold_generation=hold.hold_generation,
            queue_sequence=hold.queue_sequence))
        until(lambda: len(goal_ids) == 2 and position.x > origin[0] + 0.03)
        print("暂停位移与采样连续性通过；取消后下一单仅在明确放行后运动", flush=True)
    finally:
        node.destroy_node()


if __name__ == "__main__":
    rclpy.init()
    try:
        run()
    finally:
        rclpy.shutdown()
