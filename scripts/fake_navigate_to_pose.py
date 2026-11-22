#!/usr/bin/env python3
"""验收用的假 NavigateToPose 服务器：目标一直"在开"，直到被取消或 <控制目录>/finish 出现。

<控制目录>/events 逐行记录 received / accepted / canceled / succeeded <序号> x=<目标 x>。
<控制目录>/delay 存在时，受理要等它被删掉（最多 30 秒），用来构造"等待受理时被暂停"。
"""
import os
import sys
import time

import rclpy
from nav2_msgs.action import NavigateToPose
from rclpy.action import ActionServer, CancelResponse, GoalResponse
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import ExternalShutdownException, MultiThreadedExecutor


def main():
    ctl = sys.argv[1]
    rclpy.init()
    node = rclpy.create_node("fake_navigate_to_pose")
    count = [0]

    def has(name):
        return os.path.exists(os.path.join(ctl, name))

    def record(event, number, x):
        with open(os.path.join(ctl, "events"), "a") as out:
            out.write(f"{event} {number} x={x:.1f}\n")

    def on_goal(request):
        count[0] += 1
        record("received", count[0], request.pose.pose.position.x)
        deadline = time.monotonic() + 30
        while has("delay") and time.monotonic() < deadline:
            time.sleep(0.1)
        return GoalResponse.ACCEPT

    def execute(handle):
        number, x = count[0], handle.request.pose.pose.position.x
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
            feedback = NavigateToPose.Feedback()
            feedback.distance_remaining = 1.0
            handle.publish_feedback(feedback)
            time.sleep(0.1)
        return NavigateToPose.Result()

    server = ActionServer(
        node, NavigateToPose, "/navigate_to_pose", execute,
        goal_callback=on_goal,
        cancel_callback=lambda _: CancelResponse.ACCEPT,
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
