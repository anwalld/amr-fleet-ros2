#!/usr/bin/env python3
"""M3 死锁场景：在轻量仿真上构造一种死锁局面，判定检出与解除。

由 scripts/run_deadlock_scenarios.sh 调用（它先以 start_stations 把车摆好、拉起 fleet_sim）。
每次运行只跑一个场景的一遍，结果追加一行到 <out>/deadlock_scenarios.tsv。

场景（路网 workshop_2way）：
  head_on       两车对向：robot_1 在 jn_s 要去 jn_n（经 machining），robot_2 在 machining 要去 jn_s。
  ring3         三车成环：robot_1 在 jn_s 去 jn_se，robot_2 在 jn_se 去 machining，
                robot_3 在 machining 去 jn_s。
  outside_wait  环外有车等待：head_on 之外，robot_3 在 jn_n 要去 machining（等 robot_2），不在环上。

做法：每辆车下一张"取货点 = 自己所在站点"的 fleet_station 订单（派单按到取货点的距离选车，
所以一定派给这辆车），装货停留后各车同时申请下一段，形成预期的等待环。

判定（全部满足才 PASS）：
  1. 限定时间内 /traffic/state 的检出环数增加，且当前环上的车正是预期的那组；
  2. 限定时间内解开（resolved_count 增加，或兜底 pause 之外的动作后各车都完成本单）；
  3. 让路车在环上（环外等待的车不被选中）；
  4. 全程同一资源不出现两个占用者；
  5. 各车都完成本单（解开后交通恢复）。
服务调用一律带超时（FastDDS 高负载下会丢服务应答）。
"""

import argparse
import json
import os
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy
from std_msgs.msg import String

from robot_interfaces_business.srv import SubmitOrder
from robot_interfaces_fleet.srv import ListFleetRobots
from robot_interfaces_mission.msg import TrafficState, TrafficYield

SCENARIOS = {
    "head_on": {
        "orders": [("robot_1", "jn_s", "jn_n"), ("robot_2", "machining", "jn_s")],
        "cycle": {"robot_1", "robot_2"},
    },
    "ring3": {
        "orders": [("robot_1", "jn_s", "jn_se"), ("robot_2", "jn_se", "machining"),
                   ("robot_3", "machining", "jn_s")],
        "cycle": {"robot_1", "robot_2", "robot_3"},
    },
    "outside_wait": {
        "orders": [("robot_1", "jn_s", "jn_n"), ("robot_2", "machining", "jn_s"),
                   ("robot_3", "jn_n", "machining")],
        "cycle": {"robot_1", "robot_2"},
    },
}

TSV_HEADER = ["scenario", "round", "detected", "action", "yield_robot", "resolution_ms",
              "detect_s", "tasks_done", "violations", "verdict", "note"]


# 场景车辆的停车位（空闲时回去的站点）：放在停车区。起点都在横穿段上，若停车位等于起点，
# 办完单回"家"的车会停在别的车的终点或必经站点上（outside_wait 里 robot_3 回 jn_n 挡住
# robot_1 的终点），那是停车占位问题，不是死锁解除要测的东西。
SCENARIO_HOMES = ["park_1", "park_2", "park_3"]


def home_stations(scenario):
    """供 shell 脚本取各车停车位（与 start_stations 同序）。"""
    return ",".join(SCENARIO_HOMES[:len(SCENARIOS[scenario]["orders"])])


def start_stations(scenario):
    """供 shell 脚本取各车起点（robot_1, robot_2, ... 的顺序）。"""
    orders = SCENARIOS[scenario]["orders"]
    return ",".join(pickup for _, pickup, _ in orders)


class Scenario(Node):
    def __init__(self, args):
        super().__init__("deadlock_scenario_checker")
        self.args = args
        self.spec = SCENARIOS[args.scenario]
        self.robots = [robot for robot, _, _ in self.spec["orders"]]
        self.state = None
        self.base = None
        self.violations = []
        self.yields = []
        self.finished = set()
        self.detected_at = None
        self.detected_robots = set()
        self.cycle_from_yield = False
        self.resolved = None  # (action, robot, ms)
        self.orders_at = time.time()
        self.create_subscription(TrafficState, "/traffic/state", self.on_state, 10)
        reliable = QoSProfile(depth=50, reliability=ReliabilityPolicy.RELIABLE)
        self.create_subscription(TrafficYield, "/traffic/yield", self.on_yield, reliable)
        self.create_subscription(String, "/fleet/agent_events", self.on_event,
                                 QoSProfile(depth=100, reliability=ReliabilityPolicy.RELIABLE))
        self.submit_client = self.create_client(SubmitOrder, "/v2/submit_order")
        self.fleet_client = self.create_client(ListFleetRobots, "/v2/list_fleet_robots")

    # ------------------------------------------------------------ helpers
    def call(self, client, request, timeout):
        if not client.wait_for_service(timeout_sec=timeout):
            return None
        future = client.call_async(request)
        rclpy.spin_until_future_complete(self, future, timeout_sec=timeout)
        return future.result() if future.done() else None

    def spin_for(self, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            rclpy.spin_once(self, timeout_sec=0.1)

    # ------------------------------------------------------------ callbacks
    def on_state(self, msg):
        self.state = msg
        seen = {}
        for resource, owner in zip(msg.resource_ids, msg.owner_ids):
            if resource in seen and seen[resource] != owner:
                self.violations.append(f"{resource}: {seen[resource]} & {owner}")
            seen[resource] = owner
        if self.base is None:
            return
        if self.detected_at is None and msg.deadlock_count > self.base.deadlock_count:
            self.detected_at = time.time()
            if not self.cycle_from_yield:
                self.detected_robots = set(msg.deadlock_robot_ids)
        if self.resolved is None and msg.resolved_count > self.base.resolved_count:
            self.resolved = (msg.last_resolution_action, msg.last_resolution_robot_id,
                             msg.last_resolution_ms)

    def on_yield(self, msg):
        if self.base is not None:
            self.yields.append((msg.robot_id, msg.action, set(msg.cycle_robot_ids)))
            # /traffic/state 1 Hz 发布，常常发布时环已经被解开（deadlock_robot_ids 为空）；
            # 让路指令里带着被解的那个环，以它为准。
            if msg.cycle_robot_ids and not self.cycle_from_yield:
                self.cycle_from_yield = True
                self.detected_robots = set(msg.cycle_robot_ids)
                if self.detected_at is None:
                    self.detected_at = time.time()

    def on_event(self, msg):
        try:
            event = json.loads(msg.data)
        except json.JSONDecodeError:
            return
        if event.get("task", "").startswith(f"scn_{self.args.scenario}_"):
            self.finished.add(event.get("robot"))

    # ------------------------------------------------------------ steps
    def wait_ready(self, timeout):
        """各车 available，且停在预期起点。"""
        expected = {robot: pickup for robot, pickup, _ in self.spec["orders"]}
        deadline = time.time() + timeout
        while time.time() < deadline:
            req = ListFleetRobots.Request()
            req.include_disabled = True
            res = self.call(self.fleet_client, req, 10.0)
            if res is not None:
                ok = sum(1 for i, robot in enumerate(res.robot_ids)
                         if robot in expected and res.states[i] == "available"
                         and res.current_station_ids[i] == expected[robot])
                if ok == len(expected) and self.state is not None:
                    return True
            self.spin_for(1.0)
        return False

    def submit(self, robot, pickup, dropoff):
        req = SubmitOrder.Request()
        req.order_id = f"scn_{self.args.scenario}_{self.args.round}_{robot}"
        req.order_type = "fleet_station"
        req.priority = 10
        req.payload_json = (
            f"task_id={req.order_id};required_capability=station_transport;"
            f"pickup_station={pickup};dropoff_station={dropoff};"
            "start_if_idle=true;preempt_current=false")
        req.tags = ["deadlock_scenario"]
        res = self.call(self.submit_client, req, 10.0)
        return res is not None and res.accepted, (res.message if res is not None else "timeout")

    def run(self):
        note = []
        if not self.wait_ready(self.args.ready_timeout):
            return self.result(False, "fleet not ready")
        self.base = self.state
        self.orders_at = time.time()
        for robot, pickup, dropoff in self.spec["orders"]:
            accepted, message = self.submit(robot, pickup, dropoff)
            if not accepted:
                return self.result(False, f"order for {robot} rejected: {message}")
            self.spin_for(0.3)

        # 1. 检出
        deadline = time.time() + self.args.detect_timeout
        while self.detected_at is None and time.time() < deadline:
            rclpy.spin_once(self, timeout_sec=0.1)
        # 2–5. 解开、各车完成本单
        deadline = time.time() + self.args.finish_timeout
        while time.time() < deadline and (
                self.resolved is None or len(self.finished) < len(self.robots)):
            rclpy.spin_once(self, timeout_sec=0.1)

        detected = self.detected_at is not None
        expected_cycle = self.spec["cycle"]
        if detected and self.detected_robots != expected_cycle:
            note.append(f"cycle={','.join(sorted(self.detected_robots))}")
        yield_cmds = [y for y in self.yields if y[1] in ("reroute", "backoff")]
        yield_robot = yield_cmds[0][0] if yield_cmds else ""
        pauses = [y for y in self.yields if y[1] == "pause"]
        if pauses:
            note.append("pause")
        action, _, ms = self.resolved if self.resolved else ("", "", -1.0)
        ok = (detected and self.detected_robots == expected_cycle and self.resolved is not None
              and yield_robot in expected_cycle and not pauses and not self.violations
              and len(self.finished) == len(self.robots))
        if not ok and len(self.finished) < len(self.robots):
            note.append(f"unfinished={','.join(sorted(set(self.robots) - self.finished))}")
        return self.result(
            ok, ";".join(note), detected=detected, action=action, yield_robot=yield_robot,
            ms=ms, detect_s=(self.detected_at - self.orders_at) if detected else None)

    def result(self, ok, note, detected=False, action="", yield_robot="", ms=-1.0, detect_s=None):
        row = {
            "scenario": self.args.scenario, "round": self.args.round,
            "detected": "yes" if detected else "no", "action": action or "-",
            "yield_robot": yield_robot or "-",
            "resolution_ms": f"{ms:.0f}" if ms is not None and ms >= 0 else "-",
            "detect_s": f"{detect_s:.1f}" if detect_s is not None else "-",
            "tasks_done": f"{len(self.finished)}/{len(self.robots)}",
            "violations": len(self.violations), "verdict": "PASS" if ok else "FAIL",
            "note": note or "-",
        }
        path = os.path.join(self.args.out, "deadlock_scenarios.tsv")
        new = not os.path.exists(path)
        with open(path, "a") as f:
            if new:
                f.write("\t".join(TSV_HEADER) + "\n")
            f.write("\t".join(str(row[k]) for k in TSV_HEADER) + "\n")
        print(json.dumps(row, ensure_ascii=False))
        return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", required=True, choices=sorted(SCENARIOS))
    parser.add_argument("--round", type=int, default=1)
    parser.add_argument("--out", default=".")
    parser.add_argument("--ready-timeout", type=float, default=120.0)
    parser.add_argument("--detect-timeout", type=float, default=60.0)
    parser.add_argument("--finish-timeout", type=float, default=240.0)
    parser.add_argument("--print-start-stations", action="store_true")
    parser.add_argument("--print-home-stations", action="store_true")
    args = parser.parse_args()
    if args.print_start_stations:
        print(start_stations(args.scenario))
        return
    if args.print_home_stations:
        print(home_stations(args.scenario))
        return
    rclpy.init()
    node = Scenario(args)
    try:
        code = node.run()
    finally:
        node.destroy_node()
        rclpy.shutdown()
    sys.exit(code)


if __name__ == "__main__":
    main()
