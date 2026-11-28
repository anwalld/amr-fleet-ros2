import time

import rclpy
from action_msgs.msg import GoalStatusArray
from geometry_msgs.msg import PoseWithCovarianceStamped
from robot_interfaces.msg import LocalizationHealth, RobotState
from robot_interfaces_business.srv import SubmitOrder
from robot_interfaces_mission.srv import ReleaseRouteSegment, RequestRouteSegment

from task_control_trace import TaskControlTrace

trace = TaskControlTrace()
rclpy.init()
node = rclpy.create_node("route_hazard_probe")
observed = {"goals": []}
def observe(key, extract):
    def callback(message):
        value = extract(message)
        if observed.get(key) != value:
            trace.message(key, message)
        observed[key] = value
    return callback

node.create_subscription(RobotState, "/mission_runner/state",
                         observe("state", lambda msg: msg.state), 10)
node.create_subscription(LocalizationHealth, "/localization_health",
                         observe("localized", lambda msg: msg.localized), 10)
node.create_subscription(GoalStatusArray, "/navigate_sequence/_action/status",
                         observe("goals", lambda msg: msg.status_list), 10)
pose = node.create_publisher(PoseWithCovarianceStamped, "/amcl_pose", 10)

def until(check, seconds=8):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        rclpy.spin_once(node, timeout_sec=0.05)
        if check():
            return
    raise AssertionError(observed)

def service(name, definition, request, *, accepted_field="success"):
    client = node.create_client(definition, "/" + name)
    assert client.wait_for_service(timeout_sec=8)
    trace.message("service_request", request, service=name)
    result = client.call_async(request)
    rclpy.spin_until_future_complete(node, result, timeout_sec=6)
    assert result.done() and getattr(result.result(), accepted_field), result.result()
    trace.message("service_reply", result.result(), service=name)
    return result.result()

def localization(localized):
    sample = PoseWithCovarianceStamped()
    sample.header.frame_id = "map"
    sample.pose.pose.orientation.w = 1.0
    sample.pose.covariance[0] = sample.pose.covariance[7] = 0.01 if localized else 10.0
    pose.publish(sample)
    until(lambda: observed.get("localized") is localized)

until(lambda: pose.get_subscription_count() > 0)
localization(True)
claim = service("v2/request_route_segment", RequestRouteSegment,
                RequestRouteSegment.Request(robot_id="route_probe", to_station="packing"))
assert claim.granted, claim
service("v2/submit_order", SubmitOrder,
        SubmitOrder.Request(order_id="hazard_wait_probe", order_type="station_transport", priority=40,
                            payload_json="pickup_station=packing;dropoff_station=receiving;"
                                         "start_if_idle=true;preempt_current=false"),
        accepted_field="accepted")
until(lambda: observed.get("state") == "WAIT_ROUTE")
localization(False)
released = service("v2/release_route_segment", ReleaseRouteSegment,
                   ReleaseRouteSegment.Request(robot_id="route_probe"))
assert "robot_1" in released.granted_robot_ids, released
end = time.monotonic() + 1.5
while time.monotonic() < end:
    rclpy.spin_once(node, timeout_sec=0.05)
    assert not observed["goals"], observed
localization(True)
until(lambda: bool(observed["goals"]))
print("定位丢失期间路权已授予但没有发导航目标；定位恢复后继续", flush=True)
rclpy.shutdown()
