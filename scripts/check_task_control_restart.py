import argparse
import pathlib
import subprocess
import tempfile

import rclpy
from rclpy.action import ActionClient
from geometry_msgs.msg import PoseStamped
from robot_interfaces.action import NavigateSequence
from std_srvs.srv import Trigger

parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=("d16", "fleet"))
mode = parser.parse_args().mode
if mode == "fleet":
    from robot_interfaces_fleet.srv import ControlRemoteFleetTask
else:
    from robot_interfaces_fleet.srv import (
        ControlRemoteFleetTask, ListRemoteTasks, SubmitFleetStationTask, UpdateRemoteTaskState,
    )
    from robot_interfaces_mission.srv import (
        ListTrafficReservations, ReleaseMissionDispatchHold, ReleaseRouteSegment,
        RequestRouteSegment,
    )

rclpy.init()
node = rclpy.create_node("restart_guard_check")

def call(name, service, request=None):
    client = node.create_client(service, name)
    if not client.wait_for_service(timeout_sec=10):
        raise RuntimeError(f"service {name} unavailable")
    future = client.call_async(request or service.Request())
    rclpy.spin_until_future_complete(node, future, timeout_sec=8)
    assert future.done() and future.result() is not None, name
    return future.result()

with tempfile.TemporaryDirectory(dir=".") as temporary:
    marker = pathlib.Path(temporary).resolve()
    for restart in (False, True):
        args = ("--ros-args", "-p")
        children = []
        logfile = ("reuse" if restart else "first") + "-" + mode + ".log"
        with open(logfile, "w") as log:
            def spawn(executable, name, *flags):
                param = "restart_checkpoint_path:=" + str(marker / name)
                children.append(subprocess.Popen(("./" + executable, *args, param, *flags),
                                                 stdout=log, stderr=log))
            try:
                if mode == "fleet":
                    spawn("fleet_robot_agent_node", "agent", "-p", "mode:=task_agent",
                          "-p", "remote_task_control_enabled:=true", "-p", "robot_id:=robot_1")
                    result = call("/v2/remote_task_control/robot_1", ControlRemoteFleetTask)
                    assert result.result == ("RECOVERY_REQUIRED" if restart else "UNKNOWN"), result
                else:
                    spawn("navigate_sequence_server_node", "navigation",
                          "-p", "use_nav2_action:=false", "-p", "use_waypoint_follower:=false")
                    spawn("mission_runner_node", "runner", "-p", "mission_file:=" + str(pathlib.Path("mission.yaml").resolve()))
                    result = call("/v2/release_mission_dispatch_hold", ReleaseMissionDispatchHold)
                    assert result.held == restart, result
                    claim = call("/v2/request_route_segment", RequestRouteSegment,
                                 RequestRouteSegment.Request(robot_id="restart_probe",
                                                             to_station="receiving"))
                    reservations = call("/v2/list_traffic_reservations", ListTrafficReservations)
                    if not restart:
                        assert claim.granted and reservations.success, (claim, reservations)
                        assert "restart_probe" in reservations.owner_ids, reservations
                    if restart:
                        assert not (claim.success or claim.queued or claim.granted), claim
                        assert "reconciliation" in claim.message, claim
                        assert not reservations.success and "reconciliation" in reservations.message
                        released = call("/v2/release_route_segment", ReleaseRouteSegment,
                                        ReleaseRouteSegment.Request(robot_id="restart_probe"))
                        assert not released.success and not released.granted_robot_ids, released
                        previous = ControlRemoteFleetTask.Request(
                            task_id="previous_task", execution_id=17, revision=3)
                        receipt = call("/v2/control_remote_fleet_task", ControlRemoteFleetTask, previous)
                        assert receipt.result == "RECOVERY_REQUIRED", receipt
                        assert (receipt.task_id, receipt.execution_id, receipt.revision) == (
                            previous.task_id, previous.execution_id, previous.revision), receipt
                        tasks = call("/v2/list_remote_tasks", ListRemoteTasks)
                        assert tasks.success and not tasks.task_ids, tasks
                        claimable = call("/v2/list_remote_tasks", ListRemoteTasks,
                                         ListRemoteTasks.Request(state="DISPATCHED_REMOTE"))
                        assert not claimable.success and not claimable.task_ids, claimable
                        rejected = call("/v2/submit_fleet_station_task", SubmitFleetStationTask,
                                        SubmitFleetStationTask.Request(
                                            task_id="new_task", pickup_station="receiving",
                                            dropoff_station="packing"))
                        assert not rejected.success and "reconciliation" in rejected.message, rejected
                        update = call("/v2/update_remote_task_state", UpdateRemoteTaskState,
                                      UpdateRemoteTaskState.Request(task_id=previous.task_id,
                                                                    state="FINISHED"))
                        assert not update.success and "reconciliation" in update.message, update
                        print("restart authority refused traffic grants and unknown remote state",
                              flush=True)
                        result = call("/start_mission_profile", Trigger)
                        assert not result.success and "reconciliation" in result.message, result
                        action = ActionClient(node, NavigateSequence, "/navigate_sequence")
                        assert action.wait_for_server(timeout_sec=6)
                        goal = NavigateSequence.Goal()
                        goal.goals = [PoseStamped()]
                        future = action.send_goal_async(goal)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=8)
                        assert future.done() and not future.result().accepted
            finally:
                for child in children:
                    child.terminate()
                for child in children:
                    child.wait(timeout=5)
print(mode + " first start and repeated checkpoint verified")
rclpy.shutdown()
