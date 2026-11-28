#!/usr/bin/env python3
import argparse
import json
import os
import re
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path


ROS2_CALL_TIMEOUT = float(os.environ.get("ROS2_CALL_TIMEOUT", "30"))
SAFE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$")


def validate_safe_id(value, field_name):
    if not isinstance(value, str) or not SAFE_ID_RE.fullmatch(value) or ".." in value:
        raise ValueError(f"invalid {field_name}: {value!r}")
    return value


def safe_output_path(base_dir, filename):
    base_resolved = Path(base_dir).resolve()
    target = Path(base_dir) / filename
    if target.is_symlink():
        raise ValueError(f"refusing symlink target: {target}")
    target_resolved = target.resolve()
    if target_resolved.parent != base_resolved:
        raise ValueError(f"path escapes base directory: {target}")
    return target_resolved


def atomic_write_text(target, content):
    target_path = Path(target)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{target_path.name}.", suffix=".tmp", dir=str(target_path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp_name, target_path)
    except Exception:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


def run_ros2(args):
    # 每次调用带超时：FastDDS 高负载下会丢服务应答，不带超时会一直挂住。超时按调用失败处理。
    try:
        return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=ROS2_CALL_TIMEOUT)
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode("utf-8", errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        return subprocess.CompletedProcess(args, 124, stdout=stdout, stderr="timeout")


def response_value(text, name, default=""):
    match = re.search(rf"{name}='([^']*)'", text)
    return match.group(1) if match else default


def response_bool(text, name):
    match = re.search(rf"{name}=(True|False)", text)
    return bool(match and match.group(1) == "True")


def submit_order(order):
    payload_json = json.dumps({
        "serial_number": order.get("serial_number", "robot_1"),
        "pickup_node_id": order["pickup_node_id"],
        "dropoff_node_id": order["dropoff_node_id"],
        "start_if_idle": bool(order.get("start_if_idle", True)),
        "preempt_current": bool(order.get("preempt_current", False)),
    }, sort_keys=True)
    request = json.dumps({
        "order_id": str(order["order_id"]),
        "order_type": "vda5050",
        "priority": int(order.get("priority", 50)),
        "payload_json": payload_json,
        "tags": ["vda5050_mqtt_mock_bridge"],
    })
    return run_ros2([
        "ros2",
        "service",
        "call",
        "/v2/submit_order",
        "robot_interfaces_business/srv/SubmitOrder",
        request,
    ])


def wait_for_state(mission_id, desired_state, timeout_sec):
    deadline = time.time() + timeout_sec
    last_output = ""
    while time.time() < deadline:
        request = json.dumps({
            "limit": 80,
            "state_filter": str(desired_state),
            "mission_id_filter": str(mission_id),
        })
        result = run_ros2([
            "ros2",
            "service",
            "call",
            "/v2/list_mission_events",
            "robot_interfaces_mission/srv/ListMissionEvents",
            request,
        ])
        last_output = result.stdout
        if "success=True" in last_output and f"states=['{desired_state}']" in last_output:
            return True, last_output
        time.sleep(1.0)
    return False, last_output


def state_payload(order, mission_id, accepted, finished, submit_response, event_response):
    action_state = "FINISHED" if finished else ("ACCEPTED" if accepted else "REJECTED")
    return {
        "headerId": int(time.time()),
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "version": "2.0.0-mock",
        "manufacturer": "robot_demo",
        "serialNumber": order.get("serial_number", "robot_1"),
        "orderId": order["order_id"],
        "missionId": mission_id,
        "orderState": action_state,
        "nodeStates": [
            {"nodeId": order.get("pickup_node_id", ""), "released": True},
            {"nodeId": order.get("dropoff_node_id", ""), "released": True},
        ],
        "edgeStates": [
            {
                "edgeId": f"{order.get('pickup_node_id', '')}->{order.get('dropoff_node_id', '')}",
                "released": True,
            }
        ],
        "batteryState": {
            "batteryCharge": 80.0,
            "charging": False,
            "reach": 120.0,
        },
        "errors": [] if accepted else [{"errorType": "ORDER_REJECTED", "errorDescription": submit_response}],
        "information": [
            {
                "infoType": "source_chain",
                "infoDescription": "/v2/submit_order(vda5050) -> station order -> mission queue -> /navigate_sequence -> /v2/list_mission_events",
            }
        ],
        "rawSubmitResponse": submit_response,
        "rawEventResponse": event_response,
    }


def connection_payload(serial_number, state):
    return {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "version": "2.0.0-mock",
        "manufacturer": "robot_demo",
        "serialNumber": serial_number,
        "connectionState": state,
    }


def process_order_file(order_file, state_dir, connection_dir, timeout_sec):
    order = json.loads(order_file.read_text(encoding="utf-8"))
    order_id = validate_safe_id(order.get("order_id"), "order_id")
    serial_number = validate_safe_id(order.get("serial_number", "robot_1"), "serial_number")
    submit = submit_order(order)
    accepted = response_bool(submit.stdout, "accepted")
    mission_id = response_value(submit.stdout, "mission_id")
    finished = False
    event_response = ""
    if accepted and mission_id:
        finished, event_response = wait_for_state(mission_id, "FINISHED", timeout_sec)
    state = state_payload(order, mission_id, accepted, finished, submit.stdout.strip(), event_response.strip())
    state_dir.mkdir(parents=True, exist_ok=True)
    connection_dir.mkdir(parents=True, exist_ok=True)
    state_target = safe_output_path(state_dir, f"{order_id}.state.json")
    conn_target = safe_output_path(connection_dir, f"{serial_number}.connection.json")
    atomic_write_text(state_target, json.dumps(state, indent=2, sort_keys=True))
    atomic_write_text(
        conn_target,
        json.dumps(connection_payload(serial_number, "ONLINE"), indent=2, sort_keys=True),
    )
    return accepted and finished


def main():
    parser = argparse.ArgumentParser(description="Mock VDA 5050 MQTT bridge using files as topics")
    parser.add_argument("--mock-broker-dir", required=True)
    parser.add_argument("--timeout-sec", type=int, default=45)
    args = parser.parse_args()
    broker = Path(args.mock_broker_dir)
    order_dir = broker / "orders"
    state_dir = broker / "states"
    connection_dir = broker / "connections"
    order_files = sorted(order_dir.glob("*.json"))
    if not order_files:
        raise SystemExit("no order files in " + str(order_dir))
    ok = True
    for order_file in order_files:
        ok = process_order_file(order_file, state_dir, connection_dir, args.timeout_sec) and ok
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
