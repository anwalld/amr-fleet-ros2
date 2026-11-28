import threading
import time

import rclpy
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
from robot_interfaces.msg import RobotState
from robot_interfaces_mission.srv import SetSequencePause
from std_srvs.srv import Trigger

from task_control_trace import TaskControlTrace

trace = TaskControlTrace()
rclpy.init()
node = rclpy.create_node("pause_reply_retry_probe")
group = ReentrantCallbackGroup()
executor = MultiThreadedExecutor(num_threads=5)
executor.add_node(node)
spin_thread = threading.Thread(target=executor.spin, daemon=True)
spin_thread.start()
accepted = []
release_first_reply = threading.Event()
release_resume_reply = threading.Event()
lock = threading.Lock()

states = []


def observe_state(message):
    with lock:
        if not states or states[-1] != message.state:
            states.append(message.state)
            trace.message("mission_state", message)


subscription = node.create_subscription(RobotState, "/mission_runner/state", observe_state, 10)

def result(future, seconds=8):
    done = threading.Event()
    future.add_done_callback(lambda _: done.set())
    assert done.wait(seconds), "ROS reply timed out"
    return future.result()

backend = node.create_client(SetSequencePause, "/v2/set_sequence_pause", callback_group=group)
assert backend.wait_for_service(timeout_sec=8)

def relay(request, _):
    trace.message("navigation_pause_sent", request)
    response = result(backend.call_async(request))
    trace.message("navigation_pause_ack", response, goal_id=list(request.goal_id))
    key = (tuple(request.goal_id), request.revision, request.paused, response.success)
    with lock:
        accepted.append(key)
        first = len(accepted) == 1
    if first:
        release_first_reply.wait(4)
    if not request.paused:
        release_resume_reply.wait(4)
    trace.message("forwarded_pause_ack", response, goal_id=list(request.goal_id))
    return response

service = node.create_service(SetSequencePause, "/test/delayed_pause_reply", relay,
                              callback_group=group)

def trigger(name, expect=True):
    client = node.create_client(Trigger, "/" + name, callback_group=group)
    assert client.wait_for_service(timeout_sec=8), name
    trace.record("operator_request", service=name)
    reply = result(client.call_async(Trigger.Request()))
    trace.message("operator_reply", reply, service=name)
    assert reply.success == expect, (name, reply)

def wait_for(predicate, seconds=6):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        with lock:
            if predicate():
                return
        time.sleep(0.025)
    raise AssertionError(accepted)

try:
    trigger("start_mission_profile")
    time.sleep(0.8)
    trigger("pause_mission_profile")
    wait_for(lambda: len(accepted) >= 2)
    with lock:
        assert accepted[0] == accepted[1] and accepted[0][2:] == (True, True), accepted
    trigger("resume_mission", expect=False)
    with lock:
        start = len(states)
    trigger("resume_mission_profile")
    wait_for(lambda: any(item[2:] == (False, True) for item in accepted))
    wait_for(lambda: "RESUMING" in states[start:], seconds=1)
    time.sleep(0.3)
    with lock:
        assert states[-1] == "RESUMING" and "RUNNING" not in states[start:], states
    release_resume_reply.set()
    wait_for(lambda: states[-1] == "RUNNING")
    release_first_reply.set()
    time.sleep(1.1)
    with lock:
        assert accepted[-1][0] == accepted[0][0] and accepted[-1][1] > accepted[0][1]
        assert accepted[-1][2:] == (False, True), accepted
    print("同目标同修订重试；旧暂停回执迟到未覆盖恢复", flush=True)
finally:
    release_first_reply.set()
    release_resume_reply.set()
    executor.shutdown(timeout_sec=3)
    spin_thread.join(timeout=3)
    node.destroy_node()
    rclpy.shutdown()
