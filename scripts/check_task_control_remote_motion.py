import atexit
import math
import subprocess
import os
import time

import rclpy
from nav_msgs.msg import Odometry
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from robot_interfaces_fleet.srv import ControlRemoteFleetTask, ListRemoteTasks, SubmitFleetStationTask
from std_msgs.msg import Bool
from std_srvs.srv import SetBool
from robot_interfaces_mission.msg import TrafficState
from robot_interfaces_mission.srv import ReleaseRouteSegment, RequestRouteSegment

from task_control_trace import TaskControlTrace

trace = TaskControlTrace()
rclpy.init()
node = rclpy.create_node("remote_control_check")
state = {}
positions = []
def note_odometry(message):
    point = message.pose.pose.position
    positions.append((point.x, point.y))
    state["odom"] = message
    trace.message("odometry", message)
node.create_subscription(Odometry, "/robot_1/odom", note_odometry, 10)
def note_traffic(message):
    state["traffic"] = message
    trace.message("traffic", message)

node.create_subscription(TrafficState, "/traffic/state", note_traffic, 10)


def note_fleet_stop(message):
    state["fleet_stop"] = message.data
    trace.message("fleet_stop", message)


node.create_subscription(
    Bool, "/fleet/sim_stop/state", note_fleet_stop,
    QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
               durability=DurabilityPolicy.TRANSIENT_LOCAL),
)

def wait(check, seconds=12):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        rclpy.spin_once(node, timeout_sec=0.05)
        if check():
            return
    raise AssertionError(str(state.get("traffic")) + str(state.get("odom")))

def call(name, srv, request, ok=None):
    client = node.create_client(srv, name)
    assert client.wait_for_service(timeout_sec=8), name
    trace.message("service_request", request, service=name)
    response = client.call_async(request)
    rclpy.spin_until_future_complete(node, response, timeout_sec=5)
    assert response.done() and response.result() is not None, name
    result = response.result()
    trace.message("service_reply", result, service=name)
    assert ok is None or result.success == ok, (name, result)
    return result

def moving():
    return "odom" in state and state["odom"].twist.twist.linear.x > 0.05

def owned():
    traffic = state.get("traffic")
    return {key for key, owner in zip(traffic.resource_ids, traffic.owner_ids)
            if owner == "robot_1"} if traffic is not None else set()

def position():
    point = state["odom"].pose.pose.position
    return (point.x, point.y)

def stationary(seconds=0.5):
    origin = position()
    trace.record("stationary_window_start", position=origin, seconds=seconds)
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        rclpy.spin_once(node, timeout_sec=0.05)
        assert math.dist(origin, position()) < 0.001, (origin, position())
    trace.record("stationary_window_end", position=position())

def set_fleet_stop(enabled):
    call("/v2/set_fleet_sim_stop", SetBool, SetBool.Request(data=enabled), True)
    wait(lambda: state.get("fleet_stop") is enabled)

agent_executable = os.environ.get("FLEET_RESTART_AGENT")
agent = None
restart_while_paused = bool(os.environ.get("FLEET_RESTART_PAUSED"))


def launch_agent():
    args = [agent_executable, "--ros-args", "-p", "mode:=task_agent", "-p", "robot_id:=robot_1",
            "-p", "initial_station_id:=park_1", "-p", "home_station_id:=park_1",
            "-p", "fleet_sim_stop_enabled:=true", "-p", "remote_task_control_enabled:=true",
            "-p", "max_linear_mps:=0.55", "-p", "dwell_s:=0.1",
            "-p", "restart_checkpoint_path:=" + os.environ["FLEET_CHECKPOINT_PATH"],
            "-p", "traffic_reservation_mode:=" + os.environ.get("FLEET_RESTART_MODE", "rolling")]
    with open(os.environ["FLEET_RESTART_LOG"], "a", encoding="utf-8") as log:
        child = subprocess.Popen(args, stdout=log, stderr=log)
    trace.record("agent_started", pid=child.pid)
    return child


def stop_agent():
    if agent is not None and agent.poll() is None:
        agent.terminate()
        try:
            agent.wait(timeout=5)
        except subprocess.TimeoutExpired:
            agent.kill()
            agent.wait()
        trace.record("agent_stopped", pid=agent.pid)


if agent_executable:
    agent = launch_agent()
    atexit.register(stop_agent)

set_fleet_stop(False)
wait(lambda: "odom" in state and bool(owned()))
if os.environ.get("WARM_TASK"):
    warm_task = "warm_finish_before_cancel"
    call("/v2/submit_fleet_station_task", SubmitFleetStationTask,
         SubmitFleetStationTask.Request(task_id=warm_task, required_capability="transport",
             pickup_station="park_1", dropoff_station="park_1", priority=10), True)
    def warm_completed():
        listing = call("/v2/list_remote_tasks", ListRemoteTasks,
                       ListRemoteTasks.Request(robot_id="robot_1", state="FINISHED"))
        return warm_task in listing.task_ids
    wait(warm_completed)
task = "local_remote_control_test"
dispatched = call("/v2/submit_fleet_station_task", SubmitFleetStationTask,
                  SubmitFleetStationTask.Request(task_id=task, required_capability="transport",
                      pickup_station="receiving", dropoff_station="assembly", priority=10), True)
assert dispatched.assigned_robot_id == "robot_1", dispatched
wait(moving)
service = "/v2/control_remote_fleet_task"
Control = ControlRemoteFleetTask
def control(operation=0, revision=0, execution=0, dest="", ok=None):
    return call(service, Control, Control.Request(
        task_id=task, operation=operation, execution_id=execution, revision=revision,
        destination_station=dest), ok)
assert control().result in ("UNKNOWN", "RUNNING")
wait(lambda: control().result == "RUNNING")
execution = control(ok=True).execution_id
assert control(Control.Request.PAUSE, 1, execution, ok=True).result == "PAUSING"
wait(lambda: control(0, 1, execution).result == "PAUSED")
stationary()
retained = owned()
assert any("park_1" in key and "receiving" in key for key in retained), retained
if restart_while_paused:
    def lookup_paused():
        listing = call("/v2/list_remote_tasks", ListRemoteTasks,
                       ListRemoteTasks.Request(robot_id="robot_1", state="PAUSED"))
        return task in listing.task_ids

    wait(lookup_paused)
    before = position()
    stop_agent()
    wait(lambda: node.count_subscribers("/fleet/sim_stop/state") == 1)
    state.pop("odom")
    agent = launch_agent()
    wait(lambda: control(0, 1, execution).result == "PAUSED")
    wait(lambda: "odom" in state)
    assert math.dist(position(), before) < 0.001 and owned() == retained
    assert control().result == "HELD"
    stationary(1.2)
    print("restarted paused task: same execution, pose and route retained", flush=True)
    wait(lambda: node.count_publishers("/fleet/sim_stop/state") >= 1
         and node.count_subscribers("/fleet/sim_stop/state") >= 2)
set_fleet_stop(True)
stationary()
assert control(Control.Request.RESUME, 2, execution, ok=False).result == "REJECTED"
set_fleet_stop(False)
stationary(1.2)
assert control(Control.Request.RESUME, 2, execution, ok=False).result == "REJECTED"
assert control(Control.Request.RESUME, 3, execution, ok=True).result == "RESUMED"
wait(moving)
cancel_revision = 4
if os.environ.get("CANCEL_WHILE_PAUSED"):
    assert control(Control.Request.PAUSE, 4, execution, ok=True).result == "PAUSING"
    wait(lambda: control(0, 4, execution).result == "PAUSED")
    cancel_revision = 5
assert control(Control.Request.CANCEL, cancel_revision, execution, ok=True).result == "CANCELING"
wait(lambda: control(0, cancel_revision, execution).result == "CANCELED")
stationary()
assert retained <= owned()
if agent_executable and not restart_while_paused:
    def lookup_canceled():
        listing = call("/v2/list_remote_tasks", ListRemoteTasks,
                       ListRemoteTasks.Request(robot_id="robot_1", state="CANCELED"))
        return task in listing.task_ids

    wait(lookup_canceled)
    before = position()
    before_resources = owned()
    stop_agent()
    missing_route = os.environ.get("FLEET_RESTART_MISSING_ROUTE")
    if missing_route:
        emptied = call("/v2/release_route_segment", ReleaseRouteSegment,
                       ReleaseRouteSegment.Request(robot_id="robot_1"), True)
        assert emptied.released_resource_ids
        wait(lambda: not owned())
    state.pop("odom")
    agent = launch_agent()
    if missing_route:
        wait(lambda: control(0, cancel_revision, execution).result == "RECOVERY_REQUIRED")
        deadline = time.monotonic() + 2.5
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.05)
        assert "odom" not in state
        assert control(Control.Request.MOVE, cancel_revision + 1, execution, dest="receiving",
                       ok=False).result == "RECOVERY_REQUIRED"
        print("missing route: same checkpoint kept motion inhibited", flush=True)
        stop_agent()
        rclpy.shutdown()
        raise SystemExit(0)
    recovered = control(0, cancel_revision, execution)
    if recovered.result != "CANCELED":
        wait(lambda: control(0, cancel_revision, execution).result == "CANCELED")
    wait(lambda: "odom" in state)
    assert math.dist(position(), before) < 0.001 and owned() == before_resources
    assert control().result == "AWAITING_MOVE"
    stationary(1.2)
    print("restarted canceled hold: original receipt, pose and live route verified", flush=True)
competitor = call("/v2/request_route_segment", RequestRouteSegment,
                  RequestRouteSegment.Request(robot_id="conflict_probe", from_station="park_1",
                      to_station="receiving", priority=100))
assert not competitor.granted
stationary()
assert control(Control.Request.MOVE, cancel_revision + 1, execution, dest="receiving", ok=True).result == "MOVE_ACCEPTED"
wait(moving)
wait(lambda: not any("park_1" in key and "receiving" in key for key in owned()), seconds=35)
assert any(math.dist(sample, (6.0, 0.0)) <= 0.10 for sample in positions), position()
assert math.dist(position(), (6.0, 0.0)) <= 0.09, position()
assert control(0, cancel_revision, execution, ok=True).result == "CANCELED"
if not os.environ.get("WARM_TASK"):
    next_task = "local_remote_control_next"
    call("/v2/submit_fleet_station_task", SubmitFleetStationTask,
         SubmitFleetStationTask.Request(task_id=next_task, required_capability="transport",
             pickup_station="receiving", dropoff_station="assembly", priority=10), True)
    def next_status():
        return call(service, Control, Control.Request(task_id=next_task))
    wait(lambda: next_status().result == "RUNNING")
    assert control(Control.Request.CANCEL, cancel_revision + 2, execution, ok=False).result == "REJECTED"
    assert next_status().result == "RUNNING"
else:
    print("warm task finished; default auto-home stayed inhibited until explicit move", flush=True)
print("pause, fleet-stop rejection, replay, cancellation, route retention, explicit move and stale target passed")

rclpy.shutdown()
