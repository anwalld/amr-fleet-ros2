#!/usr/bin/env python3
"""M2 多机路权压测：随机下单 + 安全判定 + 记录。

由 scripts/run_fleet_traffic_stress.sh 调用（它先拉起 fleet_sim.launch.py）。

- 下单：同一个随机种子生成同一批订单（工作站之间随机取货/卸货），保持未完成订单数
  不超过车数；经统一入口 /v2/submit_order（order_type=fleet_station）提交，被拒（没有空闲车）
  就留在队首稍后重试。
- 判定（10 Hz）：
  1. 任意时刻一个资源只有一个占用者（/traffic/state 里同一资源不出现两次）；
  2. 同一路段上两车距离 > 0.5 m；另记录任意两车的最小距离、车在路段上却没持有该路段的次数。
- 记录：每秒一行 <out>/state.csv（占用数、排队数、死锁计数、各车位置），任务完成明细 tasks.csv，
  违规明细 violations.csv，死锁解除明细 deadlocks.csv，汇总 summary.json。
- M3 死锁指标取自 /traffic/state 的死锁统计字段与 /traffic/yield 的让路指令：
  检出次数、各解除方式次数、每次解除耗时、单次检测耗时（按 1 Hz 采样最近一次检测耗时）。
- 资源号只解析不拼接：route_edge:<a>__<b> 解析成无序站点对，再与车所在路段比对。
"""

import argparse
import csv
import json
import math
import random
import sys
import time

import rclpy
from nav_msgs.msg import Odometry
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import String

from robot_interfaces_business.srv import SubmitOrder
from robot_interfaces_fleet.srv import ListFleetRobots
from robot_interfaces_mission.msg import TrafficState, TrafficYield
from robot_interfaces_mission.srv import ListStationRoutes, ListStations

WORK_STATIONS = ["receiving", "machining", "assembly", "packing", "storage"]


def point_segment(px, py, ax, ay, bx, by):
    """返回 (点到线段距离, 投影参数 t∈[0,1])。"""
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    if length2 <= 0.0:
        return math.hypot(px - ax, py - ay), 0.0
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy)), t


def parse_edge_resource(resource_id):
    """route_edge:<a>__<b> → frozenset({a, b})；不是路段锁返回 None。"""
    prefix = "route_edge:"
    if not resource_id.startswith(prefix) or "__" not in resource_id[len(prefix):]:
        return None
    a, b = resource_id[len(prefix):].split("__", 1)
    return frozenset((a, b)) if a and b else None


def edge_name(edge):
    return "-".join(sorted(edge))


DEADLOCK_COUNTERS = ("deadlock_count", "reroute_count", "backoff_count", "pause_count",
                     "resolved_count")


class Stress(Node):
    def __init__(self, args):
        super().__init__("fleet_traffic_stress")
        self.args = args
        self.robots = [f"robot_{i + 1}" for i in range(args.robots)]
        self.rng = random.Random(args.seed)
        self.poses = {}
        self.traffic = None
        self.tasks = []
        self.violations = []
        self.pending_orders = []
        self.order_seq = 0
        self.submitted = 0
        self.submit_rejections = 0
        self.outstanding = set()
        self.min_same_edge = math.inf
        self.min_pair = math.inf
        self.unowned_edge_samples = 0
        self.unowned_since = {}
        self.unowned_long = 0
        self.samples = 0
        self.start = None
        self.end = None
        self.submit_pending = False
        self.max_queue = 0
        self.queue_samples = []
        # M3 死锁指标。
        self.counters_at_start = None
        self.last_resolved_count = None
        self.yields = []          # /traffic/yield 收到的指令（压测开始后）
        self.deadlock_rows = []   # deadlocks.csv
        self.detection_us_samples = []
        self.max_detection_us = 0.0

        for robot in self.robots:
            self.create_subscription(
                Odometry, f"/{robot}/odom", lambda msg, r=robot: self.on_odom(r, msg), 10)
        self.create_subscription(TrafficState, "/traffic/state", self.on_traffic, 10)
        self.create_subscription(
            TrafficYield, "/traffic/yield", self.on_yield,
            QoSProfile(depth=50, reliability=ReliabilityPolicy.RELIABLE))
        self.create_subscription(
            String, "/fleet/agent_events", self.on_event,
            QoSProfile(depth=100, reliability=ReliabilityPolicy.RELIABLE))
        self.submit_client = self.create_client(SubmitOrder, "/v2/submit_order")
        self.stations = {}
        self.edges = []
        self.load_map()

        out = args.out
        self.state_file = open(f"{out}/state.csv", "w", newline="")
        self.state_csv = csv.writer(self.state_file)
        header = ["t_s", "mode", "robots", "owned_resources", "waiting_entries", "grant_count",
                  "queue_count", "deadlocks", "reroute", "backoff", "pause", "resolved"]
        for robot in self.robots:
            header += [f"{robot}_x", f"{robot}_y"]
        self.state_csv.writerow(header)
        self.check_timer = self.create_timer(0.1, self.check)
        self.log_timer = self.create_timer(1.0, self.log_row)
        self.order_timer = self.create_timer(0.5, self.drive_orders)

    # ------------------------------------------------------------ setup
    def call(self, client_type, name, request, timeout=10.0):
        client = self.create_client(client_type, name)
        if not client.wait_for_service(timeout_sec=timeout):
            raise RuntimeError(f"service {name} unavailable")
        future = client.call_async(request)
        rclpy.spin_until_future_complete(self, future, timeout_sec=timeout)
        if future.result() is None:
            raise RuntimeError(f"service {name} call failed")
        return future.result()

    def load_map(self):
        stations = self.call(ListStations, "/v2/list_stations", ListStations.Request())
        for i, sid in enumerate(stations.station_ids):
            self.stations[sid] = (stations.x[i], stations.y[i])
        routes = self.call(ListStationRoutes, "/v2/list_station_routes", ListStationRoutes.Request())
        for i in range(len(routes.from_station)):
            self.edges.append((routes.from_station[i], routes.to_station[i]))

    def wait_fleet_ready(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            req = ListFleetRobots.Request()
            req.include_disabled = True
            res = self.call(ListFleetRobots, "/v2/list_fleet_robots", req)
            ready = 0
            for i, robot in enumerate(res.robot_ids):
                if robot in self.robots and res.states[i] == "available" and \
                        res.current_station_ids[i].startswith(("park_", "dock")) and \
                        robot in self.poses:
                    ready += 1
            if ready == len(self.robots):
                return True
            rclpy.spin_once(self, timeout_sec=1.0)
        return False

    # ------------------------------------------------------------ callbacks
    def on_odom(self, robot, msg):
        self.poses[robot] = (msg.pose.pose.position.x, msg.pose.pose.position.y)

    def on_traffic(self, msg):
        self.traffic = msg
        if self.start is None:
            return
        seen = {}
        for resource, owner in zip(msg.resource_ids, msg.owner_ids):
            if resource in seen and seen[resource] != owner:
                self.violation("resource_two_owners", f"{resource}: {seen[resource]} & {owner}")
            seen[resource] = owner
        waiting = len(msg.waiting_robot_ids)
        self.max_queue = max(self.max_queue, waiting)
        self.queue_samples.append(waiting)
        self.on_deadlock_stats(msg)

    def deadlock_counters(self, msg):
        return {k: getattr(msg, k) for k in DEADLOCK_COUNTERS}

    def on_deadlock_stats(self, msg):
        if self.counters_at_start is None:
            self.counters_at_start = self.deadlock_counters(msg)
            self.last_resolved_count = msg.resolved_count
            return
        if msg.last_detection_us > 0.0:
            self.detection_us_samples.append(msg.last_detection_us)
        self.max_detection_us = max(self.max_detection_us, msg.max_detection_us)
        if msg.resolved_count == self.last_resolved_count:
            return
        # 解开了新的环：1 Hz 快照只带最近一次的细节，同一秒内多次解开时中间的细节会漏（计数不漏）。
        skipped = msg.resolved_count - self.last_resolved_count - 1
        self.last_resolved_count = msg.resolved_count
        yielder = msg.last_resolution_robot_id
        cycle = next((y["cycle"] for y in reversed(self.yields) if y["robot"] == yielder
                      or y["cycle"] == yielder), "")
        self.deadlock_rows.append({
            "t_s": round(time.time() - self.start, 2),
            "cycle_robots": cycle,
            "yield_robot": yielder,
            "action": msg.last_resolution_action or "none",
            "resolution_ms": round(msg.last_resolution_ms, 1),
            "skipped_details": skipped,
        })

    def on_yield(self, msg):
        if self.start is None:
            return
        entry = {"t_s": round(time.time() - self.start, 2), "robot": msg.robot_id,
                 "action": msg.action, "cycle": ",".join(msg.cycle_robot_ids)}
        self.yields.append(entry)
        if msg.action == "pause" and msg.robot_id == (msg.cycle_robot_ids or [""])[0]:
            # 兜底暂停没有"解开"时刻，按指令逐条记一行（耗时留空）。
            self.deadlock_rows.append({
                "t_s": entry["t_s"], "cycle_robots": entry["cycle"], "yield_robot": "",
                "action": "pause", "resolution_ms": "", "skipped_details": 0})

    def on_event(self, msg):
        try:
            event = json.loads(msg.data)
        except json.JSONDecodeError:
            return
        if self.start is None:
            return
        event["t_s"] = round(time.time() - self.start, 3)
        self.tasks.append(event)
        self.outstanding.discard(event.get("task"))

    def violation(self, kind, detail):
        t = round(time.time() - self.start, 2) if self.start else 0.0
        self.violations.append({"t_s": t, "kind": kind, "detail": detail})
        self.get_logger().error(f"VIOLATION {kind}: {detail}")

    # ------------------------------------------------------------ orders
    def next_order(self):
        pickup, dropoff = self.rng.sample(WORK_STATIONS, 2)
        self.order_seq += 1
        return {"task_id": f"stress_{self.order_seq:06d}", "pickup": pickup, "dropoff": dropoff}

    def drive_orders(self):
        if self.start is None or self.submit_pending:
            return
        if time.time() >= self.end:
            return
        if not self.pending_orders and len(self.outstanding) < len(self.robots):
            self.pending_orders.append(self.next_order())
        if not self.pending_orders:
            return
        order = self.pending_orders[0]
        req = SubmitOrder.Request()
        req.order_id = order["task_id"]
        req.order_type = "fleet_station"
        req.priority = 10
        req.payload_json = (
            f"task_id={order['task_id']};required_capability=station_transport;"
            f"pickup_station={order['pickup']};dropoff_station={order['dropoff']};"
            "start_if_idle=true;preempt_current=false")
        req.tags = ["fleet_traffic_stress"]
        self.submit_pending = True
        future = self.submit_client.call_async(req)
        future.add_done_callback(lambda f, o=order: self.on_submitted(f, o))

    def on_submitted(self, future, order):
        self.submit_pending = False
        res = future.result()
        if res is not None and res.accepted:
            self.pending_orders.pop(0)
            self.submitted += 1
            self.outstanding.add(order["task_id"])
        else:
            # 拿不到（没有空闲车 / 被拒）：留在队首，稍后重试。
            self.submit_rejections += 1

    # ------------------------------------------------------------ checks
    def robot_edge(self, x, y):
        best = None
        for a, b in self.edges:
            if a not in self.stations or b not in self.stations:
                continue
            ax, ay = self.stations[a]
            bx, by = self.stations[b]
            d, t = point_segment(x, y, ax, ay, bx, by)
            if d < 0.3 and 0.05 < t < 0.95 and (best is None or d < best[0]):
                best = (d, frozenset((a, b)))
        return best[1] if best else None

    def check(self):
        if self.start is None or len(self.poses) < len(self.robots):
            return
        self.samples += 1
        owners = {}
        if self.traffic is not None:
            for resource, owner in zip(self.traffic.resource_ids, self.traffic.owner_ids):
                edge = parse_edge_resource(resource)
                if edge is not None:
                    owners[edge] = owner
        on_edge = {}
        for robot, (x, y) in self.poses.items():
            edge = self.robot_edge(x, y)
            unowned = False
            if edge is not None:
                on_edge[robot] = edge
                if owners and owners.get(edge) != robot:
                    self.unowned_edge_samples += 1
                    unowned = True
            # /traffic/state 是 1 Hz 快照：刚授予就出发的那一秒会显示未持有，持续 > 1.5 s 才计。
            now = time.time()
            if unowned:
                since = self.unowned_since.setdefault(robot, now)
                if now - since > 1.5 and since >= 0:
                    self.unowned_long += 1
                    self.violation("robot_on_unowned_edge", f"{robot} on {edge_name(edge)} > 1.5s")
                    self.unowned_since[robot] = -1.0  # 同一次只记一回
            else:
                self.unowned_since.pop(robot, None)
        robots = list(self.poses)
        for i in range(len(robots)):
            for j in range(i + 1, len(robots)):
                a, b = robots[i], robots[j]
                d = math.dist(self.poses[a], self.poses[b])
                self.min_pair = min(self.min_pair, d)
                if a in on_edge and on_edge.get(a) == on_edge.get(b):
                    self.min_same_edge = min(self.min_same_edge, d)
                    if d <= 0.5:
                        self.violation(
                            "same_edge_distance", f"{a}-{b} on {edge_name(on_edge[a])} d={d:.2f}")

    def log_row(self):
        if self.start is None:
            return
        t = self.traffic
        row = [round(time.time() - self.start, 2), self.args.mode, len(self.robots),
               len(t.resource_ids) if t else 0, len(t.waiting_robot_ids) if t else 0,
               t.grant_count if t else 0, t.queue_count if t else 0]
        base = self.counters_at_start or {}
        row += [(getattr(t, k) - base.get(k, 0)) if t else 0 for k in DEADLOCK_COUNTERS]
        for robot in self.robots:
            x, y = self.poses.get(robot, (float("nan"), float("nan")))
            row += [round(x, 3), round(y, 3)]
        self.state_csv.writerow(row)
        self.state_file.flush()

    # ------------------------------------------------------------ run
    def run(self):
        if not self.wait_fleet_ready(self.args.ready_timeout):
            self.get_logger().error("fleet agents did not become ready")
            return 2
        self.start = time.time()
        self.end = self.start + self.args.minutes * 60.0
        # 死锁计数以压测开始时为零点（就绪阶段的计数不算）。
        self.counters_at_start = None
        self.get_logger().info(
            f"stress start: robots={len(self.robots)} mode={self.args.mode} "
            f"minutes={self.args.minutes}")
        while rclpy.ok() and time.time() < self.end:
            rclpy.spin_once(self, timeout_sec=0.1)
        return self.finish()

    def finish(self):
        duration_s = time.time() - self.start
        finished = [t for t in self.tasks if t.get("finished", 0) > 0]
        with open(f"{self.args.out}/tasks.csv", "w", newline="") as f:
            fields = ["t_s", "robot", "task", "pickup", "dropoff", "mode", "duration_s",
                      "wait_route_s", "distance_m"]
            writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(finished)
        with open(f"{self.args.out}/violations.csv", "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["t_s", "kind", "detail"])
            writer.writeheader()
            writer.writerows(self.violations)
        with open(f"{self.args.out}/deadlocks.csv", "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["t_s", "cycle_robots", "yield_robot", "action",
                                                   "resolution_ms", "skipped_details"])
            writer.writeheader()
            writer.writerows(self.deadlock_rows)
        mean = lambda xs: sum(xs) / len(xs) if xs else 0.0
        base = self.counters_at_start or {}
        final = self.deadlock_counters(self.traffic) if self.traffic else {}
        delta = {k: final.get(k, 0) - base.get(k, 0) for k in DEADLOCK_COUNTERS}
        resolve_ms = [r["resolution_ms"] for r in self.deadlock_rows
                      if isinstance(r["resolution_ms"], float) and r["resolution_ms"] >= 0.0]
        summary = {
            "robots": len(self.robots),
            "mode": self.args.mode,
            "network": self.args.network,
            "seed": self.args.seed,
            "duration_s": round(duration_s, 1),
            "orders_submitted": self.submitted,
            "submit_rejections_retried": self.submit_rejections,
            "tasks_finished": len(finished),
            # 最后一单完成的时刻；远早于结束说明车队停摆（whole_route 下会出现永久互等）。
            "last_task_finished_t_s": max((t["t_s"] for t in finished), default=None),
            "tasks_finished_after_5min": sum(1 for t in finished if t["t_s"] >= 300.0),
            "throughput_tasks_per_hour": round(len(finished) / duration_s * 3600.0, 1),
            "mean_wait_route_s": round(mean([t["wait_route_s"] for t in finished]), 2),
            "mean_task_duration_s": round(mean([t["duration_s"] for t in finished]), 2),
            "wait_share_of_duration": round(
                mean([t["wait_route_s"] for t in finished]) /
                max(mean([t["duration_s"] for t in finished]), 1e-9), 3),
            "violations": len(self.violations),
            "violations_by_kind": {
                k: sum(1 for v in self.violations if v["kind"] == k)
                for k in sorted({v["kind"] for v in self.violations})},
            "min_same_edge_distance_m": None if math.isinf(self.min_same_edge)
            else round(self.min_same_edge, 3),
            "min_pair_distance_m": None if math.isinf(self.min_pair) else round(self.min_pair, 3),
            "robot_on_unowned_edge_samples_1hz_lag": self.unowned_edge_samples,
            "robot_on_unowned_edge_over_1_5s": self.unowned_long,
            "check_samples": self.samples,
            "max_waiting_entries": self.max_queue,
            "mean_waiting_entries": round(mean(self.queue_samples), 2),
            "final_grant_count": self.traffic.grant_count if self.traffic else 0,
            "final_queue_count": self.traffic.queue_count if self.traffic else 0,
            # M3 死锁指标（键名与 scripts/nightly.sh 阶段 5 的 metrics.tsv 对齐）。
            "deadlock_resolution_enabled": self.args.deadlock_resolution,
            "deadlocks_detected": delta["deadlock_count"],
            "deadlock_reroute": delta["reroute_count"],
            "deadlock_backoff": delta["backoff_count"],
            "deadlock_pause": delta["pause_count"],
            "deadlock_resolved": delta["resolved_count"],
            "resolution_ms_mean": round(mean(resolve_ms), 1) if resolve_ms else None,
            "resolution_ms_max": round(max(resolve_ms), 1) if resolve_ms else None,
            "detection_us_mean": round(mean(self.detection_us_samples), 1)
            if self.detection_us_samples else None,
            "detection_us_max": round(self.max_detection_us, 1) if self.traffic else None,
        }
        with open(f"{self.args.out}/summary.json", "w") as f:
            json.dump(summary, f, indent=2, ensure_ascii=False)
        print(json.dumps(summary, ensure_ascii=False))
        return 0 if not self.violations else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--robots", type=int, required=True)
    parser.add_argument("--minutes", type=float, required=True)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--seed", type=int, default=20261116)
    parser.add_argument("--ready-timeout", type=float, default=120.0)
    parser.add_argument("--network", default="workshop")
    parser.add_argument("--deadlock-resolution", default="true")
    args = parser.parse_args()
    rclpy.init()
    node = Stress(args)
    try:
        code = node.run()
    finally:
        node.state_file.close()
        node.destroy_node()
        rclpy.shutdown()
    sys.exit(code)


if __name__ == "__main__":
    main()
