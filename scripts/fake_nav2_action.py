#!/usr/bin/env python3
"""验收用的假 Nav2 动作服务器：目标一直"在开"，直到被取消或 <控制目录>/finish 出现。

用法：fake_nav2_action.py <控制目录> <navigate_to_pose|follow_waypoints>

<控制目录>/events 逐行记录 received / accepted / cancel_rejected / canceled / succeeded <序号> x=<目标 x>。
<控制目录>/reject_cancel 存在时拒绝取消请求，用来代替"取消请求丢了"。
<控制目录>/delay 存在时，受理要等它被删掉（最多 30 秒），用来构造"等待受理时被暂停"。
"""
import os
import sys
import time

import rclpy
from nav2_msgs.action import FollowWaypoints, NavigateToPose
from rclpy.action import ActionServer, CancelResponse, GoalResponse
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import ExternalShutdownException, MultiThreadedExecutor


def main():
    ctl, name = sys.argv[1], sys.argv[2]
    waypoints = name == "follow_waypoints"
    action = FollowWaypoints if waypoints else NavigateToPose
    rclpy.init()
    node = rclpy.create_node(f"fake_{name}")
    count = [0]

    def has(flag):
        return os.path.exists(os.path.join(ctl, flag))

    def target_x(request):
        return (request.poses[0] if waypoints else request.pose).pose.position.x

    def record(event, number, x):
        with open(os.path.join(ctl, "events"), "a") as out:
            out.write(f"{event} {number} x={x:.1f}\n")

    def on_goal(request):
        count[0] += 1
        record("received", count[0], target_x(request))
        deadline = time.monotonic() + 30
        while has("delay") and time.monotonic() < deadline:
            time.sleep(0.1)
        return GoalResponse.ACCEPT

    def on_cancel(handle):
        if has("reject_cancel"):
            record("cancel_rejected", count[0], target_x(handle.request))
            return CancelResponse.REJECT
        return CancelResponse.ACCEPT

    def execute(handle):
        number, x = count[0], target_x(handle.request)
        record("accepted", number, x)
        while rclpy.ok():
            if handle.is_cancel_requested:
                record("canceled", number, x)
                handle.canceled()
                break
            if has("finish"):
                record("succeeded", number, x)
                handle.succeed()
                break
            feedback = action.Feedback()
            if not waypoints:
                feedback.distance_remaining = 1.0
            handle.publish_feedback(feedback)
            time.sleep(0.1)
        return action.Result()

    server = ActionServer(
        node, action, f"/{name}", execute,
        goal_callback=on_goal,
        cancel_callback=on_cancel,
        callback_group=ReentrantCallbackGroup(),
    )
    executor = MultiThreadedExecutor(num_threads=4)
    executor.add_node(node)
    try:
        executor.spin()
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        server.destroy()


if __name__ == "__main__":
    main()
