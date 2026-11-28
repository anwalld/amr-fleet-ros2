#!/usr/bin/env python3
import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import subprocess
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

MAX_BODY_BYTES = 1024 * 1024


class PayloadTooLargeError(Exception):
    pass


class InvalidContentLengthError(Exception):
    pass


def yaml_bool(value):
    return "true" if bool(value) else "false"


def yaml_scalar(value):
    text = str(value)
    if not text:
      return "''"
    if re.fullmatch(r"[A-Za-z0-9_./:-]+", text):
        return text
    return json.dumps(text)


def yaml_list(values):
    return "[" + ", ".join(yaml_scalar(value) for value in values) + "]"


def submit_order_request(order_id, order_type, priority, payload, tags=None):
    return json.dumps({
        "order_id": str(order_id),
        "order_type": str(order_type),
        "priority": int(priority),
        "payload_json": json.dumps(payload, sort_keys=True),
        "tags": [str(tag) for tag in (tags or [])],
    })


def request_body_hash(payload):
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def run_ros2(args, timeout_sec=30):
    try:
        result = subprocess.run(
            args, check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout_sec)
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode("utf-8", errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        return {
            "returncode": 124,
            "stdout": stdout,
            "stderr": f"ros2 call timed out after {timeout_sec}s",
            "ok": False,
            "timed_out": True,
        }
    return {
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
        "ok": result.returncode == 0,
        "timed_out": False,
    }


def http_status_for_ros2(result):
    if result.get("ok"):
        return 200
    if result.get("timed_out"):
        return 504
    return 502


def response_bool(text, name, default=False):
    match = re.search(rf"{name}=(True|False)", text)
    return (match.group(1) == "True") if match else default


def response_int(text, name, default=0):
    match = re.search(rf"{name}=(-?\d+)", text)
    return int(match.group(1)) if match else default


def response_float(text, name, default=0.0):
    match = re.search(rf"{name}=(-?\d+(?:\.\d+)?)", text)
    return float(match.group(1)) if match else default


def response_string(text, name, default=""):
    match = re.search(rf"{name}='([^']*)'", text)
    return match.group(1) if match else default


def response_list(text, name):
    values = []
    for match in re.finditer(rf"(?<![A-Za-z_]){name}=\[(.*?)\](?:,|\))", text, re.S):
        try:
            parsed = ast.literal_eval("[" + match.group(1) + "]")
        except (SyntaxError, ValueError):
            parsed = []
        values.extend(parsed)
    return values


def service_call(service_name, service_type, request, timeout_sec=30):
    return run_ros2(
        ["ros2", "service", "call", service_name, service_type, request],
        timeout_sec=timeout_sec)


def load_policy(path):
    if not path:
        return {
            "operators": {"system": "supervisor"},
            "roles": {"supervisor": ["*"]},
        }
    return json.loads(Path(path).read_text(encoding="utf-8"))


def load_json_file(path, default):
    if not path:
        return default
    file_path = Path(path)
    if not file_path.exists():
        return default
    return json.loads(file_path.read_text(encoding="utf-8"))


def write_json_file(path, payload):
    if not path:
        return
    file_path = Path(path)
    file_path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{file_path.name}.", suffix=".tmp", dir=str(file_path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(json.dumps(payload, indent=2, sort_keys=True))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp_name, file_path)
    except Exception:
        try:
            os.unlink(tmp_name)
        except OSError:
            pass
        raise


def parse_events(stdout):
    stamps = response_list(stdout, "stamp")
    mission_ids = response_list(stdout, "mission_ids")
    states = response_list(stdout, "states")
    previous_states = response_list(stdout, "previous_states")
    messages = response_list(stdout, "messages")
    recoverable = response_list(stdout, "recoverable")
    events = []
    for index, mission_id in enumerate(mission_ids):
        events.append({
            "stamp": stamps[index] if index < len(stamps) else "",
            "mission_id": mission_id,
            "state": states[index] if index < len(states) else "",
            "previous_state": previous_states[index] if index < len(previous_states) else "",
            "message": messages[index] if index < len(messages) else "",
            "recoverable": bool(recoverable[index]) if index < len(recoverable) else True,
        })
    return events


def business_route_metadata(business_type):
    result = service_call(
        "/v2/list_business_order_types", "robot_interfaces_business/srv/ListBusinessOrderTypes", "{}")
    if not result["ok"]:
        return "", ""
    business_types = response_list(result["stdout"], "business_types")
    scenario_ids = response_list(result["stdout"], "scenario_ids")
    workflow_ids = response_list(result["stdout"], "workflow_ids")
    for index, value in enumerate(business_types):
        if value == business_type:
            scenario_id = scenario_ids[index] if index < len(scenario_ids) else ""
            workflow_id = workflow_ids[index] if index < len(workflow_ids) else ""
            return scenario_id, workflow_id
    return "", ""


class RestGatewayHandler(BaseHTTPRequestHandler):
    server_version = "RobotRestGateway/0.2"

    def log_message(self, fmt, *args):
        if self.server.verbose:
            super().log_message(fmt, *args)

    def send_json(self, status, payload):
        body = json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header(
            "Access-Control-Allow-Headers", "Content-Type, Idempotency-Key, X-Operator-Id")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header(
            "Access-Control-Allow-Headers", "Content-Type, Idempotency-Key, X-Operator-Id")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def read_json(self):
        raw_length = self.headers.get("Content-Length", "0")
        try:
            length = int(raw_length)
        except ValueError as exc:
            raise InvalidContentLengthError("invalid Content-Length") from exc
        if length < 0:
            raise InvalidContentLengthError("negative Content-Length")
        if length > MAX_BODY_BYTES:
            raise PayloadTooLargeError(f"payload exceeds {MAX_BODY_BYTES} bytes")
        if length == 0:
            return {}
        data = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(data, dict):
            raise json.JSONDecodeError("JSON body must be an object", "", 0)
        return data

    def idempotency_key_from(self, payload):
        return self.headers.get("Idempotency-Key", "") or payload.get("idempotency_key", "")

    def idempotency_record_key(self, path, key):
        return f"{path}:{key}"

    def begin_idempotent_request(self, path, payload, operator_id, role):
        key = self.idempotency_key_from(payload)
        if not key:
            return False, None
        record_key = self.idempotency_record_key(path, key)
        req_hash = request_body_hash(payload)
        with self.server.idempotency_cond:
            while record_key in self.server.idempotency_inflight:
                if self.server.idempotency_inflight[record_key] != req_hash:
                    self.write_audit(
                        operator_id, role, "POST", path, True, 409,
                        "idempotency key conflict for " + key)
                    self.send_json(409, {
                        "success": False,
                        "message": "idempotency key conflict: request body differs",
                        "idempotency_key": key,
                    })
                    return True, None
                self.server.idempotency_cond.wait()
            record = self.server.idempotency_store.get(record_key)
            if record is not None:
                stored_hash = record.get("request_hash")
                if stored_hash and stored_hash != req_hash:
                    self.write_audit(
                        operator_id, role, "POST", path, True, 409,
                        "idempotency key conflict for " + key)
                    self.send_json(409, {
                        "success": False,
                        "message": "idempotency key conflict: request body differs",
                        "idempotency_key": key,
                    })
                    return True, None
                response_payload = dict(record.get("payload", {}))
                response_payload["idempotency_key"] = key
                response_payload["idempotent_replay"] = True
                status = int(record.get("status", 200))
                self.write_audit(
                    operator_id, role, "POST", path, True, status,
                    "idempotent replay for " + key)
                self.send_json(status, response_payload)
                return True, None
            self.server.idempotency_inflight[record_key] = req_hash
            return False, (record_key, key, req_hash)

    def finish_idempotent_request(self, slot, path, payload, status=None, response_payload=None):
        if slot is None:
            return
        record_key, key, req_hash = slot
        with self.server.idempotency_cond:
            try:
                if status is not None and status < 500 and response_payload is not None:
                    record = {
                        "path": path,
                        "idempotency_key": key,
                        "request_hash": req_hash,
                        "status": status,
                        "payload": response_payload,
                        "stored_at": datetime.now(timezone.utc).isoformat(),
                        "external_correlation_id": payload.get("external_correlation_id", ""),
                    }
                    self.server.idempotency_store[record_key] = record
                    write_json_file(self.server.idempotency_store_path, self.server.idempotency_store)
            finally:
                self.server.idempotency_inflight.pop(record_key, None)
                self.server.idempotency_cond.notify_all()

    def operator_id_from(self, payload=None, query=None):
        if "X-Operator-Id" in self.headers:
            return self.headers["X-Operator-Id"]
        if payload:
            return payload.get("operator_id") or payload.get("requester_id") or self.server.default_operator_id
        if query and "operator_id" in query:
            return query["operator_id"][0]
        return self.server.default_operator_id

    def authorize(self, operator_id, method, path):
        if not self.server.enforce_roles:
            return True, "role enforcement disabled", "unrestricted"
        role = self.server.role_policy.get("operators", {}).get(operator_id, "")
        allowed_patterns = self.server.role_policy.get("roles", {}).get(role, [])
        action = f"{method} {path}"
        allowed = "*" in allowed_patterns or action in allowed_patterns or path in allowed_patterns
        if allowed:
            return True, "allowed", role
        return False, f"operator {operator_id} role {role or 'unknown'} cannot {action}", role

    def write_audit(self, operator_id, role, method, path, allowed, status, message):
        if not self.server.audit_log:
            return
        record = {
            "timestamp": datetime.now(timezone.utc).isoformat(),
            "operator_id": operator_id,
            "role": role,
            "method": method,
            "path": path,
            "allowed": allowed,
            "status": status,
            "message": message,
        }
        with self.server.audit_lock:
            Path(self.server.audit_log).parent.mkdir(parents=True, exist_ok=True)
            with open(self.server.audit_log, "a", encoding="utf-8") as handle:
                handle.write(json.dumps(record, sort_keys=True) + "\n")

    def reject_if_unauthorized(self, operator_id, method, path):
        allowed, message, role = self.authorize(operator_id, method, path)
        if allowed:
            return False, role
        self.write_audit(operator_id, role, method, path, False, 403, message)
        self.send_json(403, {
            "success": False,
            "message": message,
            "operator_id": operator_id,
            "role": role,
        })
        return True, role

    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        operator_id = self.operator_id_from(query=query)
        if parsed.path == "/health":
            self.send_json(200, {"success": True, "message": "ok"})
            return
        rejected, role = self.reject_if_unauthorized(operator_id, "GET", parsed.path)
        if rejected:
            return
        if parsed.path == "/api/missions/events":
            limit = int(query.get("limit", ["20"])[0])
            state_filter = query.get("state_filter", [""])[0]
            mission_id_filter = query.get("mission_id_filter", [""])[0]
            request = (
                "{"
                f"limit: {limit}, "
                f"state_filter: {yaml_scalar(state_filter)}, "
                f"mission_id_filter: {yaml_scalar(mission_id_filter)}"
                "}"
            )
            result = service_call(
                "/v2/list_mission_events", "robot_interfaces_mission/srv/ListMissionEvents", request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "GET", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "events": parse_events(result["stdout"]),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/fleet/robots":
            capability = query.get("capability", [""])[0]
            include_disabled = query.get("include_disabled", ["false"])[0].lower() == "true"
            request = (
                "{"
                f"capability: {yaml_scalar(capability)}, "
                f"include_disabled: {yaml_bool(include_disabled)}"
                "}"
            )
            result = service_call(
                "/v2/list_fleet_robots", "robot_interfaces_fleet/srv/ListFleetRobots", request)
            robots = []
            ids = response_list(result["stdout"], "robot_ids")
            enabled = response_list(result["stdout"], "enabled")
            states = response_list(result["stdout"], "states")
            battery = response_list(result["stdout"], "battery_voltage")
            stations = response_list(result["stdout"], "current_station_ids")
            capabilities = response_list(result["stdout"], "capabilities")
            for index, robot_id in enumerate(ids):
                robots.append({
                    "robot_id": robot_id,
                    "enabled": bool(enabled[index]) if index < len(enabled) else False,
                    "state": states[index] if index < len(states) else "",
                    "battery_voltage": battery[index] if index < len(battery) else 0.0,
                    "current_station_id": stations[index] if index < len(stations) else "",
                    "capabilities": capabilities[index] if index < len(capabilities) else "",
                })
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "GET", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "robots": robots,
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/facilities/resources":
            resource_type = query.get("resource_type", [""])[0]
            include_disabled = query.get("include_disabled", ["false"])[0].lower() == "true"
            request = (
                "{"
                f"resource_type: {yaml_scalar(resource_type)}, "
                f"include_disabled: {yaml_bool(include_disabled)}"
                "}"
            )
            result = service_call(
                "/v2/list_facility_resources",
                "robot_interfaces_facility/srv/ListFacilityResources",
                request)
            ids = response_list(result["stdout"], "resource_ids")
            types = response_list(result["stdout"], "resource_types")
            station_ids = response_list(result["stdout"], "station_ids")
            available = response_list(result["stdout"], "available")
            status = response_list(result["stdout"], "status")
            resources = []
            for index, resource_id in enumerate(ids):
                resources.append({
                    "resource_id": resource_id,
                    "resource_type": types[index] if index < len(types) else "",
                    "station_id": station_ids[index] if index < len(station_ids) else "",
                    "available": bool(available[index]) if index < len(available) else False,
                    "status": status[index] if index < len(status) else "",
                })
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "GET", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "resources": resources,
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/workflows/status":
            request = "{order_id: " + yaml_scalar(query.get("order_id", [""])[0]) + "}"
            result = service_call(
                "/v2/get_workflow_status", "robot_interfaces_business/srv/GetWorkflowStatus", request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "GET", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "order_id": response_string(result["stdout"], "order_id"),
                "state": response_string(result["stdout"], "state"),
                "total_steps": response_int(result["stdout"], "total_steps"),
                "finished_steps": response_int(result["stdout"], "finished_steps"),
                "queued_steps": response_int(result["stdout"], "queued_steps"),
                "running_mission_id": response_string(result["stdout"], "running_mission_id"),
                "queued_mission_ids": response_list(result["stdout"], "queued_mission_ids"),
                "finished_mission_ids": response_list(result["stdout"], "finished_mission_ids"),
                "paused": response_bool(result["stdout"], "paused"),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/operator/snapshot":
            event_limit = int(query.get("event_limit", ["20"])[0])
            result = service_call(
                "/v2/get_operator_snapshot",
                "robot_interfaces_business/srv/GetOperatorSnapshot",
                "{event_limit: " + str(event_limit) + "}")
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "GET", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "mission": {
                    "state": response_string(result["stdout"], "mission_state"),
                    "previous_state": response_string(result["stdout"], "previous_mission_state"),
                    "active_mission_id": response_string(result["stdout"], "active_mission_id"),
                    "message": response_string(result["stdout"], "mission_message"),
                    "recoverable": response_bool(result["stdout"], "recoverable", True),
                    "active": response_bool(result["stdout"], "mission_active"),
                    "paused": response_bool(result["stdout"], "paused"),
                },
                "queue": {
                    "size": response_int(result["stdout"], "queue_size"),
                    "mission_ids": response_list(result["stdout"], "queued_mission_ids"),
                    "priorities": response_list(result["stdout"], "queued_priorities"),
                },
                "paused_workflow_order_ids": response_list(
                    result["stdout"], "paused_workflow_order_ids"),
                "fleet_robot_ids": response_list(result["stdout"], "fleet_robot_ids"),
                "remote_tasks": [
                    {
                        "task_id": task_id,
                        "robot_id": response_list(result["stdout"], "remote_task_robot_ids")[index]
                        if index < len(response_list(result["stdout"], "remote_task_robot_ids"))
                        else "",
                        "state": response_list(result["stdout"], "remote_task_states")[index]
                        if index < len(response_list(result["stdout"], "remote_task_states"))
                        else "",
                    }
                    for index, task_id in enumerate(
                        response_list(result["stdout"], "remote_task_ids"))
                ],
                "facility_resource_ids": response_list(result["stdout"], "facility_resource_ids"),
                "business_types": response_list(result["stdout"], "business_types"),
                "recent_events": parse_events(
                    result["stdout"]
                    .replace("recent_event_stamp", "stamp")
                    .replace("recent_event_mission_ids", "mission_ids")
                    .replace("recent_event_states", "states")
                    .replace("recent_event_messages", "messages")),
                "raw_response": result["stdout"],
            })
            return
        self.write_audit(operator_id, role, "GET", parsed.path, True, 404, "not found")
        self.send_json(404, {"success": False, "message": "not found"})

    def do_POST(self):
        parsed = urlparse(self.path)
        try:
            payload = self.read_json()
        except PayloadTooLargeError as exc:
            self.send_json(413, {"success": False, "message": str(exc)})
            return
        except (InvalidContentLengthError, json.JSONDecodeError, UnicodeDecodeError) as exc:
            self.send_json(400, {"success": False, "message": str(exc)})
            return
        operator_id = self.operator_id_from(payload=payload)
        rejected, role = self.reject_if_unauthorized(operator_id, "POST", parsed.path)
        if rejected:
            return
        if parsed.path == "/api/business_orders":
            handled, slot = self.begin_idempotent_request(parsed.path, payload, operator_id, role)
            if handled:
                return
            status = None
            response_payload = None
            try:
                business_order_id = payload.get("business_order_id", "")
                business_type = payload.get("business_type", "")
                request = submit_order_request(
                    business_order_id,
                    "business",
                    payload.get("priority", 0),
                    {
                        "business_order_id": business_order_id,
                        "business_type": business_type,
                        "start_if_idle": bool(payload.get("start_if_idle", True)),
                        "preempt_current": bool(payload.get("preempt_current", False)),
                    },
                    tags=["rest_gateway", "business_order"],
                )
                result = service_call(
                    "/v2/submit_order", "robot_interfaces_business/srv/SubmitOrder", request)
                status = http_status_for_ros2(result)
                routed_scenario_id, routed_workflow_id = (
                    business_route_metadata(business_type) if result["ok"] else ("", "")
                )
                self.write_audit(
                    operator_id, role, "POST", parsed.path, True, status,
                    response_string(result["stdout"], "message", result["stderr"]))
                response_payload = {
                    "success": result["ok"] and response_bool(result["stdout"], "accepted"),
                    "message": response_string(result["stdout"], "message", result["stderr"]),
                    "business_order_id": business_order_id,
                    "business_type": business_type,
                    "routed_scenario_id": routed_scenario_id,
                    "routed_workflow_id": routed_workflow_id,
                    "mission_ids": response_list(result["stdout"], "mission_ids"),
                    "queue_size": response_int(result["stdout"], "queue_size"),
                    "external_correlation_id": payload.get("external_correlation_id", ""),
                    "raw_response": result["stdout"],
                }
            finally:
                self.finish_idempotent_request(slot, parsed.path, payload, status, response_payload)
            self.send_json(status, response_payload)
            return
        if parsed.path == "/api/station_order_batches":
            handled, slot = self.begin_idempotent_request(parsed.path, payload, operator_id, role)
            if handled:
                return
            status = None
            response_payload = None
            try:
                request = submit_order_request(
                    payload.get("batch_id", ""),
                    "station_order_batch",
                    payload.get("priority", 0),
                    {
                        "batch_id": payload.get("batch_id", ""),
                        "order_ids": payload.get("order_ids", []),
                        "pickup_station_ids": payload.get("pickup_station_ids", []),
                        "dropoff_station_ids": payload.get("dropoff_station_ids", []),
                        "start_if_idle": bool(payload.get("start_if_idle", True)),
                        "preempt_current": bool(payload.get("preempt_current", False)),
                        "continue_on_error": bool(payload.get("continue_on_error", False)),
                    },
                    tags=["rest_gateway", "station_order_batch"],
                )
                result = service_call(
                    "/v2/submit_order",
                    "robot_interfaces_business/srv/SubmitOrder",
                    request)
                status = http_status_for_ros2(result)
                mission_ids = response_list(result["stdout"], "mission_ids")
                accepted = result["ok"] and response_bool(result["stdout"], "accepted")
                self.write_audit(
                    operator_id, role, "POST", parsed.path, True, status,
                    response_string(result["stdout"], "message", result["stderr"]))
                response_payload = {
                    "success": accepted,
                    "message": response_string(result["stdout"], "message", result["stderr"]),
                    "mission_ids": mission_ids,
                    "accepted_count": len(mission_ids),
                    "rejected_count": 0 if accepted else 1,
                    "queue_size": response_int(result["stdout"], "queue_size"),
                    "external_correlation_id": payload.get("external_correlation_id", ""),
                    "raw_response": result["stdout"],
                }
            finally:
                self.finish_idempotent_request(slot, parsed.path, payload, status, response_payload)
            self.send_json(status, response_payload)
            return
        if parsed.path == "/api/workflows/cancel":
            request = (
                "{"
                f"order_id: {yaml_scalar(payload.get('order_id', ''))}, "
                f"cancel_active: {yaml_bool(payload.get('cancel_active', True))}"
                "}"
            )
            result = service_call(
                "/v2/cancel_workflow", "robot_interfaces_business/srv/CancelWorkflow", request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "POST", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "canceled_mission_ids": response_list(result["stdout"], "canceled_mission_ids"),
                "queue_size": response_int(result["stdout"], "queue_size"),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/workflows/pause":
            request = (
                "{"
                f"order_id: {yaml_scalar(payload.get('order_id', ''))}, "
                f"pause_active: {yaml_bool(payload.get('pause_active', True))}"
                "}"
            )
            result = service_call(
                "/v2/pause_workflow", "robot_interfaces_business/srv/PauseWorkflow", request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "POST", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "affected_mission_ids": response_list(result["stdout"], "affected_mission_ids"),
                "active_pause_requested": response_bool(
                    result["stdout"], "active_pause_requested"),
                "queue_size": response_int(result["stdout"], "queue_size"),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/workflows/resume":
            request = (
                "{"
                f"order_id: {yaml_scalar(payload.get('order_id', ''))}, "
                f"resume_active: {yaml_bool(payload.get('resume_active', True))}"
                "}"
            )
            result = service_call(
                "/v2/resume_workflow", "robot_interfaces_business/srv/ResumeWorkflow", request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "POST", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "affected_mission_ids": response_list(result["stdout"], "affected_mission_ids"),
                "active_resume_requested": response_bool(
                    result["stdout"], "active_resume_requested"),
                "queue_size": response_int(result["stdout"], "queue_size"),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/missions/reprioritize":
            request = (
                "{"
                f"mission_id: {yaml_scalar(payload.get('mission_id', ''))}, "
                f"priority: {int(payload.get('priority', 0))}"
                "}"
            )
            result = service_call(
                "/v2/reprioritize_queued_mission",
                "robot_interfaces_mission/srv/ReprioritizeQueuedMission",
                request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "POST", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "queue_size": response_int(result["stdout"], "queue_size"),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/missions/reprioritize_batch":
            request = (
                "{"
                f"mission_ids: {yaml_list(payload.get('mission_ids', []))}, "
                f"priorities: {yaml_list(payload.get('priorities', []))}"
                "}"
            )
            result = service_call(
                "/v2/reprioritize_batch",
                "robot_interfaces_mission/srv/ReprioritizeBatch",
                request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "POST", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "updated_mission_ids": response_list(result["stdout"], "updated_mission_ids"),
                "queue_size": response_int(result["stdout"], "queue_size"),
                "raw_response": result["stdout"],
            })
            return
        if parsed.path == "/api/remote_tasks/state":
            request = (
                "{"
                f"task_id: {yaml_scalar(payload.get('task_id', ''))}, "
                f"state: {yaml_scalar(payload.get('state', ''))}, "
                f"message: {yaml_scalar(payload.get('message', ''))}"
                "}"
            )
            result = service_call(
                "/v2/update_remote_task_state",
                "robot_interfaces_fleet/srv/UpdateRemoteTaskState",
                request)
            status = http_status_for_ros2(result)
            self.write_audit(
                operator_id, role, "POST", parsed.path, True, status,
                response_string(result["stdout"], "message", result["stderr"]))
            self.send_json(status, {
                "success": result["ok"] and response_bool(result["stdout"], "success"),
                "message": response_string(result["stdout"], "message", result["stderr"]),
                "task_id": response_string(result["stdout"], "task_id"),
                "assigned_robot_id": response_string(result["stdout"], "assigned_robot_id"),
                "state": response_string(result["stdout"], "state"),
                "raw_response": result["stdout"],
            })
            return
        self.write_audit(operator_id, role, "POST", parsed.path, True, 404, "not found")
        self.send_json(404, {"success": False, "message": "not found"})


def create_server(host="127.0.0.1", port=18080, role_policy="", enforce_roles=False,
                  default_operator_id="system", audit_log="", idempotency_store="", verbose=False):
    server = ThreadingHTTPServer((host, port), RestGatewayHandler)
    server.verbose = verbose
    server.role_policy = load_policy(role_policy)
    server.enforce_roles = enforce_roles
    server.default_operator_id = default_operator_id
    server.audit_log = audit_log
    server.audit_lock = threading.Lock()
    server.idempotency_store_path = idempotency_store
    server.idempotency_store = load_json_file(idempotency_store, {})
    server.idempotency_cond = threading.Condition()
    server.idempotency_lock = server.idempotency_cond
    server.idempotency_inflight = {}
    return server


def main():
    parser = argparse.ArgumentParser(description="Thin HTTP gateway for existing robot ROS services")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--role-policy", default="")
    parser.add_argument("--enforce-roles", action="store_true")
    parser.add_argument("--default-operator-id", default="system")
    parser.add_argument("--audit-log", default="")
    parser.add_argument("--idempotency-store", default="")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    server = create_server(
        host=args.host,
        port=args.port,
        role_policy=args.role_policy,
        enforce_roles=args.enforce_roles,
        default_operator_id=args.default_operator_id,
        audit_log=args.audit_log,
        idempotency_store=args.idempotency_store,
        verbose=args.verbose,
    )
    print(f"rest api gateway listening on http://{args.host}:{args.port}", flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
