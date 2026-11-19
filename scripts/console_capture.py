#!/usr/bin/env python3
"""调度台回放采集（M4，夜跑阶段 5、6 与压测 / 场景并行运行）。

订阅与轮询的对象、频率都和 robot_console 的 RosBridge 一致，按回放格式（JSONL，一行一条）写文件：
  meta      第一行：格式版本、路网、车数、模式、被测提交号（环境变量 NIGHTLY_REV）、来源运行
  graph     /v2/list_stations + /v2/list_station_routes：启动时一次，之后每 5 秒重查，有变化才写
  pose      每车 /<id>/odom（车辆列表来自快照，可用 --odom-override 覆盖话题），只留 x/y/yaw/vx/wz 与原始 stamp
  traffic   /traffic/state 完整消息
  snapshot  /v2/get_operator_snapshot 完整应答，每秒一次（同时最多挂一个请求，3 秒超时）
  event     /fleet/agent_events（JSON 字符串，解析后写入）
字段名与 ROS 接口一致（rosidl_runtime_py.message_to_ordereddict）。格式定义见
openspec 规格 operator-console「回放文件格式」。

用法：
  python3 scripts/console_capture.py --out console.jsonl --network workshop_2way --robots 6 \
      --mode rolling --source "~/nightly/2026-11-20/stage5_traffic_stress/stress_6_rolling"
收到 SIGINT / SIGTERM 时刷盘并以退出码 0 退出；--duration N 表示 N 秒后自行退出。
"""

import argparse
import json
import math
import os
import signal
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, qos_profile_sensor_data
from rclpy.signals import SignalHandlerOptions
from rosidl_runtime_py.convert import message_to_ordereddict

from nav_msgs.msg import Odometry
from robot_interfaces_business.srv import GetOperatorSnapshot
from robot_interfaces_mission.msg import TrafficState
from robot_interfaces_mission.srv import ListStationRoutes, ListStations
from std_msgs.msg import String

FORMAT_VERSION = 1
REQUEST_TIMEOUT_S = 3.0


def clean(value):
    """JSON 不允许 NaN / Inf，换成 null。"""
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {k: clean(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [clean(v) for v in value]
    return value


def yaw_from_quaternion(q):
    return math.atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.y * q.y + q.z * q.z))


def stamp_seconds(stamp):
    return stamp.sec + stamp.nanosec * 1e-9


def pose_record(t, robot, odom):
    return {
        "t": round(t, 4),
        "kind": "pose",
        "robot": robot,
        "stamp": stamp_seconds(odom.header.stamp),
        "x": odom.pose.pose.position.x,
        "y": odom.pose.pose.position.y,
        "yaw": yaw_from_quaternion(odom.pose.pose.orientation),
        "vx": odom.twist.twist.linear.x,
        "wz": odom.twist.twist.angular.z,
    }


def parse_overrides(items):
    out = {}
    for item in items or []:
        robot, sep, topic = item.partition("=")
        if sep and robot and topic:
            out[robot] = topic
    return out


class PendingRequest:
    """每类请求同时最多挂一个；超过 3 秒放弃，下一轮照常发。"""

    def __init__(self):
        self.future = None
        self.sent = 0.0
        self.timeouts = 0

    def busy(self, now):
        if self.future is None:
            return False
        if self.future.done():
            return False
        if now - self.sent >= REQUEST_TIMEOUT_S:
            self.future.cancel()
            self.future = None
            self.timeouts += 1
            return False
        return True


class Capture(Node):
    def __init__(self, args, out):
        super().__init__("console_capture")
        self.out = out
        self.t0 = time.monotonic()
        self.overrides = parse_overrides(args.odom_override)
        self.event_limit = args.event_limit
        self.lines = 0
        self.odom_subs = {}
        self.last_graph = None
        self.graph_parts = {}

        self.write({
            "t": 0.0, "kind": "meta", "v": FORMAT_VERSION, "network": args.network,
            "robots": args.robots, "mode": args.mode, "rev": os.environ.get("NIGHTLY_REV", ""),
            "source": args.source, "captured_by": "scripts/console_capture.py",
            "started_unix": time.time(),
        })

        reliable10 = QoSProfile(depth=10, reliability=ReliabilityPolicy.RELIABLE)
        reliable100 = QoSProfile(depth=100, reliability=ReliabilityPolicy.RELIABLE)
        self.create_subscription(TrafficState, "/traffic/state", self.on_traffic, reliable10)
        self.create_subscription(String, "/fleet/agent_events", self.on_event, reliable100)

        self.snapshot_client = self.create_client(GetOperatorSnapshot, "/v2/get_operator_snapshot")
        self.stations_client = self.create_client(ListStations, "/v2/list_stations")
        self.routes_client = self.create_client(ListStationRoutes, "/v2/list_station_routes")
        self.snapshot_req = PendingRequest()
        self.stations_req = PendingRequest()
        self.routes_req = PendingRequest()

        self.create_timer(1.0, self.poll_snapshot)
        self.create_timer(5.0, self.poll_graph)
        self.first_graph_timer = self.create_timer(0.3, self.first_graph)
        self.create_timer(1.0, self.out.flush)

    def now(self):
        return time.monotonic() - self.t0

    def write(self, record):
        self.out.write(json.dumps(clean(record), ensure_ascii=False, separators=(",", ":")))
        self.out.write("\n")
        self.lines += 1

    # ---- 话题 ----
    def on_traffic(self, msg):
        self.write({"t": round(self.now(), 4), "kind": "traffic", "data": message_to_ordereddict(msg)})

    def on_event(self, msg):
        try:
            data = json.loads(msg.data)
            if not isinstance(data, dict):
                data = {"raw": msg.data}
        except ValueError:
            data = {"raw": msg.data}
        self.write({"t": round(self.now(), 4), "kind": "event", "data": data})

    def ensure_odom(self, robot_ids):
        for robot in robot_ids:
            if robot in self.odom_subs:
                continue
            topic = self.overrides.get(robot, f"/{robot}/odom")
            self.odom_subs[robot] = self.create_subscription(
                Odometry, topic, lambda msg, r=robot: self.write(pose_record(self.now(), r, msg)),
                qos_profile_sensor_data)
            self.get_logger().info(f"订阅 {robot} 的位置：{topic}")

    # ---- 快照 ----
    def poll_snapshot(self):
        if self.snapshot_req.busy(time.monotonic()) or not self.snapshot_client.service_is_ready():
            return
        req = GetOperatorSnapshot.Request()
        req.event_limit = self.event_limit
        future = self.snapshot_client.call_async(req)
        future.add_done_callback(self.on_snapshot)
        self.snapshot_req.future = future
        self.snapshot_req.sent = time.monotonic()

    def on_snapshot(self, future):
        if future.cancelled() or future.exception() is not None:
            return
        res = future.result()
        self.ensure_odom(list(res.fleet_robot_ids))
        self.write({"t": round(self.now(), 4), "kind": "snapshot", "data": message_to_ordereddict(res)})

    # ---- 路网 ----
    def first_graph(self):
        self.first_graph_timer.cancel()
        self.poll_graph()

    def poll_graph(self):
        now = time.monotonic()
        if self.stations_req.busy(now) or self.routes_req.busy(now):
            return
        if not (self.stations_client.service_is_ready() and self.routes_client.service_is_ready()):
            return
        self.graph_parts = {}
        for name, client, srv, pending in (
                ("stations", self.stations_client, ListStations, self.stations_req),
                ("routes", self.routes_client, ListStationRoutes, self.routes_req)):
            future = client.call_async(srv.Request())
            future.add_done_callback(lambda f, n=name: self.on_graph_part(n, f))
            pending.future = future
            pending.sent = now

    def on_graph_part(self, name, future):
        if future.cancelled() or future.exception() is not None:
            return
        self.graph_parts[name] = message_to_ordereddict(future.result())
        if len(self.graph_parts) < 2:
            return
        stations, routes = self.graph_parts["stations"], self.graph_parts["routes"]
        self.graph_parts = {}
        if not (stations.get("success") and routes.get("success")):
            return
        key = json.dumps([stations, routes], sort_keys=True)
        if key == self.last_graph:
            return
        self.last_graph = key
        self.write({"t": round(self.now(), 4), "kind": "graph", "stations": stations, "routes": routes})

    def summary(self):
        return (f"lines={self.lines} robots={len(self.odom_subs)} "
                f"timeouts(snapshot/stations/routes)={self.snapshot_req.timeouts}/"
                f"{self.stations_req.timeouts}/{self.routes_req.timeouts}")


def main():
    parser = argparse.ArgumentParser(description="调度台回放采集（JSONL）")
    parser.add_argument("--out", required=True, help="输出文件")
    parser.add_argument("--network", default="")
    parser.add_argument("--robots", type=int, default=0)
    parser.add_argument("--mode", default="")
    parser.add_argument("--source", default="", help="来源运行（写进 meta）")
    parser.add_argument("--odom-override", action="append", default=[],
                        help="按车辆号覆盖位置话题，如 robot_1=/odom，可重复")
    parser.add_argument("--event-limit", type=int, default=20, help="快照请求的 event_limit")
    parser.add_argument("--duration", type=float, default=0.0, help="N 秒后自行退出（0 = 直到收到信号）")
    args = parser.parse_args()

    stop = {"flag": False}

    def on_signal(signum, _frame):
        stop["flag"] = True

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    with open(args.out, "w", encoding="utf-8") as out:
        node = Capture(args, out)
        started = time.monotonic()
        try:
            while rclpy.ok() and not stop["flag"]:
                rclpy.spin_once(node, timeout_sec=0.1)
                if args.duration > 0 and time.monotonic() - started >= args.duration:
                    break
        finally:
            out.flush()
            print(f"console_capture: {node.summary()} -> {args.out}", file=sys.stderr)
            node.destroy_node()
            if rclpy.ok():
                rclpy.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
