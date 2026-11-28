"""Exercise the durable mission index and old-goal reconciliation on real ROS nodes."""

import argparse
from contextlib import ExitStack
from pathlib import Path
import subprocess
import time

import rclpy
from robot_interfaces.msg import RobotState
from robot_interfaces_fleet.srv import (
    ControlRemoteFleetTask,
    ListRemoteTasks,
    SubmitFleetStationTask,
    UpdateFleetRobotState,
    UpdateRemoteTaskState,
)
from robot_interfaces_mission.srv import EnqueueMission, RequestRouteSegment
from std_srvs.srv import Trigger


class Probe:
    def __init__(self, primary, navigation, stations, output):
        self.runner = primary
        self.navigation = navigation
        self.stations = stations
        self.output = output
        self.node = rclpy.create_node("mission_restart_reconciliation_probe")
        self.children = ExitStack()
        self.state = ""

    def close(self):
        self.children.close()
        self.node.destroy_node()

    @staticmethod
    def stop(child):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()

    def launch(self, executable, label, **parameters):
        arguments = [str(executable), "--ros-args"]
        for key, value in parameters.items():
            arguments += ["-p", f"{key}:={value}"]
        with (self.output / f"{label}.log").open("w", encoding="utf-8") as stream:
            child = subprocess.Popen(arguments, stdout=stream, stderr=stream)
        self.children.callback(self.stop, child)
        return child

    def call(self, name, service=Trigger, request=None):
        client = self.node.create_client(service, name)
        try:
            assert client.wait_for_service(timeout_sec=8), name
            future = client.call_async(request or service.Request())
            rclpy.spin_until_future_complete(self.node, future, timeout_sec=6)
            assert future.done() and future.result() is not None, name
            return future.result()
        finally:
            self.node.destroy_client(client)

    def wait(self, predicate, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.05)
            if predicate():
                return
        raise AssertionError("condition timed out", self.state)

    def run_dispatcher(self):
        checkpoint = self.output / "dispatcher"
        arguments = dict(robot_id="dispatcher", fleet_size=1,
                         station_file=self.stations, restart_checkpoint_path=checkpoint,
                         autostart="false", return_to_dock_on_low_battery="false")
        runner = self.launch(self.runner, "dispatcher-first", **arguments)

        agent_queries = []

        def reply(request, response):
            agent_queries.append((request.operation, request.revision))
            response.success = request.revision in (12, 13)
            response.task_id = request.task_id
            response.execution_id = request.execution_id
            response.revision = request.revision
            response.latest_revision = request.revision
            response.result = "PAUSED" if response.success else "UNKNOWN"
            response.message = "confirmed by original agent" if response.success else "not recorded"
            return response

        control = ControlRemoteFleetTask
        self.node.create_service(control, "/v2/remote_task_control/robot_1", reply)
        alive = self.call("/v2/update_fleet_robot_state", UpdateFleetRobotState,
                          UpdateFleetRobotState.Request(robot_id="robot_1", state="available",
                              update_state=True, update_current_station_id=True,
                              current_station_id="park_1"))
        assert alive.success, alive
        assigned = self.call("/v2/submit_fleet_station_task", SubmitFleetStationTask,
                             SubmitFleetStationTask.Request(task_id="reboot_remote",
                               pickup_station="receiving", dropoff_station="assembly",
                               required_capability="transport"))
        assert assigned.success and assigned.assigned_robot_id == "robot_1", assigned
        update = self.call("/v2/update_remote_task_state", UpdateRemoteTaskState,
                           UpdateRemoteTaskState.Request(task_id="reboot_remote", state="PAUSED"))
        assert update.success, update
        key = dict(task_id="reboot_remote", execution_id=281, revision=12)
        original = self.call("/v2/control_remote_fleet_task", control,
                             control.Request(operation=control.Request.QUERY, **key))
        assert original.success and original.result == "PAUSED", original
        self.stop(runner)
        self.launch(self.runner, "dispatcher-restarted", **arguments)
        found = self.call("/v2/list_remote_tasks", ListRemoteTasks)
        assert found.success and found.task_ids == ["reboot_remote"]
        assert found.assigned_robot_ids == ["robot_1"] and found.states == ["PAUSED"], found
        claim = self.call("/v2/list_remote_tasks", ListRemoteTasks,
                          ListRemoteTasks.Request(robot_id="robot_1", state="DISPATCHED_REMOTE"))
        assert not claim.success and not claim.task_ids, claim
        known = self.call("/v2/control_remote_fleet_task", control, control.Request(**key))
        assert known == original and agent_queries == [(control.Request.QUERY, 12)], known
        missing_local = self.call("/v2/control_remote_fleet_task", control,
                                  control.Request(**dict(key, revision=13)))
        assert missing_local.success and missing_local.result == "PAUSED", missing_local
        changed = dict(key, revision=14)
        unknown = self.call("/v2/control_remote_fleet_task", control, control.Request(**changed))
        assert not unknown.success and unknown.result == "RECOVERY_REQUIRED", unknown
        resume = self.call("/v2/control_remote_fleet_task", control,
                           control.Request(operation=control.Request.RESUME, **changed))
        assert not resume.success and resume.result == "RECOVERY_REQUIRED", resume
        assert agent_queries == [(control.Request.QUERY, revision) for revision in (12, 13, 14)]
        print("dispatcher: original assignment, persisted and live receipts recovered; motion writes blocked")

    def run_d16(self):
        checkpoint = self.output / "runner"
        profile = self.output / "active.yaml"
        goals = "".join(f"  - x: {i}.0\n    y: 0.0\n    yaw: 0.0\n" for i in range(1, 40))
        profile.write_text("mission_id: active_before_restart\nframe_id: map\nloop: false\ngoals:\n"
                           + goals, encoding="utf-8")
        queued = self.output / "queued.yaml"
        queued.write_text(profile.read_text().replace("active_before_restart", "queued_before_restart"),
                          encoding="utf-8")
        self.state = ""

        def observe(message):
            self.state = message.state

        self.node.create_subscription(RobotState, "/mission_runner/state", observe, 10)
        self.launch(self.navigation, "nav", use_nav2_action="false", use_waypoint_follower="false")
        arguments = dict(mission_file=profile, restart_checkpoint_path=checkpoint,
                         autostart="false", preflight_enabled="false",
                         return_to_dock_on_low_battery="false")
        runner = self.launch(self.runner, "runner-first", **arguments)
        assert self.call("/start_mission_profile").success
        self.wait(lambda: self.state == "RUNNING")
        next_mission = self.call("/v2/enqueue_mission_profile", EnqueueMission,
                                 EnqueueMission.Request(mission_file=str(queued), priority=42))
        assert next_mission.success, next_mission
        assert self.call("/pause_mission_profile").success
        self.wait(lambda: self.state == "PAUSED")
        self.stop(runner)
        self.launch(self.runner, "runner-restarted", **arguments)

        def observed():
            result = self.call("/v2/get_mission_restart_state")
            return result if "observed_goal_state=PAUSE_ACKNOWLEDGED" in result.message else None

        self.wait(observed)
        report = self.call("/v2/get_mission_restart_state")
        assert report.success and 'mission="active_before_restart"' in report.message, report
        assert 'queued="queued_before_restart" 42' in report.message, report
        assert 'pause_mask=1' in report.message, report
        assert not self.call("/start_mission_profile").success
        direct = self.call("/v2/request_route_segment", RequestRouteSegment,
                           RequestRouteSegment.Request(robot_id="other", to_station="receiving"))
        assert not direct.granted and not direct.queued, direct
        (self.output / "recovered-state.txt").write_text(report.message + "\n", encoding="utf-8")
        print("D16: original live UUID and pause rechecked, pending queue recovered, dispatch held")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=("d16", "dispatcher"))
    parser.add_argument("runner", type=Path)
    parser.add_argument("navigation", type=Path)
    parser.add_argument("stations", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    rclpy.init()
    probe = Probe(args.runner, args.navigation, args.stations, args.output)
    try:
        if args.case == "d16":
            probe.run_d16()
        else:
            probe.run_dispatcher()
    finally:
        probe.close()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
