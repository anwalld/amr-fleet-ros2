#!/usr/bin/env python3
"""Offline unit tests for static audit fixes across Dagu, REST/Outbox, and bridge scripts."""

import hashlib
import http.client
import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

SCRIPTS_DIR = Path(__file__).resolve().parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import dagu_brief_remote
import dagu_chat
import dagu_nightly
import dagu_nightly_evidence
import external_callback_outbox
import facility_plc_file_adapter
import rest_api_gateway
import vda5050_file_bridge
import vda5050_instant_action_bridge
import vda5050_mqtt_mock_bridge


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class SourceManifestTests(unittest.TestCase):
    def test_verify_source_manifest_pass_mismatch_missing_and_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ws = root / "workspace"
            out = root / "output"
            ws.mkdir()
            out.mkdir()
            (ws / "a.txt").write_bytes(b"hello\n")
            (ws / "sub").mkdir()
            (ws / "sub/b.txt").write_bytes(b"world\n")

            h_a = sha256_bytes(b"hello\n")
            h_b = sha256_bytes(b"world\n")
            manifest = ws / "source.manifest"
            manifest.write_text(
                f"{h_a}  ./a.txt\n"
                f"{h_b}  sub/b.txt\n",
                encoding="utf-8",
            )

            verdict = dagu_nightly_evidence.verify_evidence(out, ws, [1, 8], 30, 3, "")
            self.assertEqual(verdict["issues"], [])
            self.assertIn("source_manifest", verdict["checks"])
            self.assertTrue(verdict["checks"]["source_manifest"]["ok"])
            self.assertEqual(verdict["checks"]["source_manifest"]["checked_files"], 2)

            # Tamper a.txt and remove sub/b.txt
            (ws / "a.txt").write_bytes(b"tampered\n")
            (ws / "sub/b.txt").unlink()
            verdict2 = dagu_nightly_evidence.verify_evidence(out, ws, [1, 8], 30, 3, "")
            self.assertFalse(verdict2["checks"]["source_manifest"]["ok"])
            self.assertEqual(verdict2["checks"]["source_manifest"]["mismatched_count"], 1)
            self.assertEqual(verdict2["checks"]["source_manifest"]["missing_count"], 1)
            self.assertTrue(any("失配" in i for i in verdict2["issues"]))
            self.assertTrue(any("缺失" in i for i in verdict2["issues"]))

            # Path traversal entry in manifest must be rejected
            manifest.write_text(
                f"{sha256_bytes(b'x')}  ../escape.txt\n",
                encoding="utf-8",
            )
            details, issues = dagu_nightly_evidence.verify_source_manifest(manifest, ws)
            self.assertFalse(details["ok"])
            self.assertEqual(details["mismatched_count"], 1)
            self.assertTrue(issues)


class DaguResilienceTests(unittest.TestCase):
    def test_terminate_child_group_escalates_sigterm_to_sigkill(self):
        # Child ignores SIGTERM and sleeps; terminate_child_group must SIGKILL it promptly.
        code = "import signal, time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(60)"
        child = subprocess.Popen(
            [sys.executable, "-c", code],
            start_new_session=True,
        )
        try:
            time.sleep(0.15)
            dagu_nightly.terminate_child_group(child, term_timeout=0.3, kill_timeout=2.0)
            self.assertIsNotNone(child.poll())
            self.assertEqual(child.returncode, -signal.SIGKILL)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()

    def test_brief_remote_handles_blocked_blank_and_corrupt_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            ws = root / "ws"
            out = root / "2026-11-27-run-blocked"
            ws.mkdir()
            out.mkdir()
            (out / "checks.jsonl").write_text(
                "\n"
                "{not valid json\n"
                + json.dumps({
                    "checked_at": "2026-11-27T10:00:00+08:00",
                    "state": "blocked",
                    "reason": "defect escalation",
                }) + "\n"
                "\n",
                encoding="utf-8",
            )
            res = dagu_brief_remote.read_run(out, ws, "read", "-")
            self.assertTrue(res["terminal"])
            self.assertEqual(res["run_id"], "run-blocked")
            self.assertIn("已阻塞", res["brief"])
            self.assertIn("defect escalation", res["brief"])

    def test_brief_remote_handles_native_status_code_4(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            ws = root / "ws"
            out = root / "2026-11-27-run4"
            ws.mkdir()
            out.mkdir()
            plan = {
                "checked_at": "2026-11-27T10:00:00+08:00",
                "state": "planned",
                "run_id": "run4",
                "revision": "abc123",
                "output_dir": str(out),
                "stages": [1, 8],
                "dry_run": False,
                "scenario_rounds": 3,
            }
            (out / "checks.jsonl").write_text(
                "{corrupt\n" + json.dumps(plan) + "\n",
                encoding="utf-8",
            )
            status_dir = root / "control/dagu/data/dag-runs/robot-nightly/dag-runs/2026/11/27/dag-run_x_run4/a_1"
            status_dir.mkdir(parents=True)
            (status_dir / "status.jsonl").write_text(
                json.dumps({
                    "name": "robot-nightly",
                    "dagRunId": "run4",
                    "paramsList": [f"ROBOT_WORKSPACE={ws}", "NIGHTLY_REV=abc123"],
                    "createdAt": 1000,
                    "status": 4,
                    "startedAt": "2026-11-27T10:00:05+08:00",
                    "finishedAt": "2026-11-27T10:10:00+08:00",
                }) + "\n",
                encoding="utf-8",
            )
            res = dagu_brief_remote.read_run(out, ws, "read", "-")
            self.assertTrue(res["terminal"])
            self.assertIn("通过", res["brief"])

    def test_dagu_chat_show_status_ignores_corrupted_state_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            state_dir = Path(tmp)
            (state_dir / "bad1.json").write_text("{broken json", encoding="utf-8")
            (state_dir / "bad2.json").write_text(json.dumps({"no_output": 1}), encoding="utf-8")
            (state_dir / "good.json").write_text(
                json.dumps({"output": "/tmp/2026-11-27-myrun", "host": "h", "workspace": "/w"}),
                encoding="utf-8",
            )
            with mock.patch.object(dagu_chat, "STATE", state_dir), \
                 mock.patch.object(dagu_chat, "remote", return_value={"brief": "myrun 通过"}) as mock_remote:
                with mock.patch("builtins.print") as mock_print:
                    dagu_chat.show_status("myrun")
                mock_remote.assert_called_once()
                mock_print.assert_any_call("myrun 通过")


class RestApiGatewayTests(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.tmp = Path(self.tmpdir.name)
        self.idem_path = self.tmp / "idem.json"
        self.audit_path = self.tmp / "audit.jsonl"
        self.server = rest_api_gateway.create_server(
            host="127.0.0.1",
            port=0,
            audit_log=str(self.audit_path),
            idempotency_store=str(self.idem_path),
        )
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.tmpdir.cleanup()

    def post(self, path, body_bytes, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        hdrs = {"Content-Type": "application/json"}
        if headers:
            hdrs.update(headers)
        conn.request("POST", path, body=body_bytes, headers=hdrs)
        resp = conn.getresponse()
        data = json.loads(resp.read().decode("utf-8"))
        status = resp.status
        conn.close()
        return status, data

    def test_single_flight_409_conflict_and_atomic_store(self):
        call_count = 0
        call_lock = threading.Lock()
        started = threading.Event()
        release = threading.Event()

        def fake_service_call(service_name, service_type, request, timeout_sec=30):
            nonlocal call_count
            if service_name == "/v2/submit_order":
                with call_lock:
                    call_count += 1
                started.set()
                release.wait(timeout=5)
                return {
                    "ok": True,
                    "timed_out": False,
                    "returncode": 0,
                    "stdout": "accepted=True, message='ok', mission_ids=['m1'], queue_size=1",
                    "stderr": "",
                }
            return {"ok": True, "timed_out": False, "returncode": 0, "stdout": "", "stderr": ""}

        payload = {"business_order_id": "bo-1", "business_type": "campus_mail"}
        body = json.dumps(payload).encode("utf-8")
        results = []

        def worker():
            results.append(self.post("/api/business_orders", body, {"Idempotency-Key": "k-sf"}))

        with mock.patch.object(rest_api_gateway, "service_call", side_effect=fake_service_call):
            t1 = threading.Thread(target=worker)
            t2 = threading.Thread(target=worker)
            t1.start()
            self.assertTrue(started.wait(timeout=3))
            t2.start()
            # Different body while in-flight -> immediate 409
            diff_status, diff_data = self.post(
                "/api/business_orders",
                json.dumps({"business_order_id": "bo-OTHER", "business_type": "campus_mail"}).encode("utf-8"),
                {"Idempotency-Key": "k-sf"},
            )
            self.assertEqual(diff_status, 409)
            self.assertFalse(diff_data["success"])

            release.set()
            t1.join(timeout=5)
            t2.join(timeout=5)

            self.assertEqual(call_count, 1)
            self.assertEqual(len(results), 2)
            self.assertTrue(all(st == 200 and d["success"] for st, d in results))
            self.assertEqual(sum(1 for _, d in results if d.get("idempotent_replay") is True), 1)

            # Different body after completion -> 409
            conflict_status, _ = self.post(
                "/api/business_orders",
                json.dumps({"business_order_id": "bo-2", "business_type": "campus_mail"}).encode("utf-8"),
                {"Idempotency-Key": "k-sf"},
            )
            self.assertEqual(conflict_status, 409)

        # Verify atomic write left no .tmp files and persisted valid JSON
        self.assertTrue(self.idem_path.is_file())
        self.assertEqual(list(self.tmp.glob("*.tmp")), [])
        stored = json.loads(self.idem_path.read_text(encoding="utf-8"))
        self.assertIn("/api/business_orders:k-sf", stored)
        self.assertIn("request_hash", stored["/api/business_orders:k-sf"])

    def test_5xx_and_timeout_not_cached_and_business_false_returns_200(self):
        # 1. Timeout -> 504, not cached
        with mock.patch.object(
            rest_api_gateway.subprocess,
            "run",
            side_effect=subprocess.TimeoutExpired(["ros2"], 30),
        ):
            st, data = self.post(
                "/api/business_orders",
                json.dumps({"business_order_id": "bo-t", "business_type": "campus_mail"}).encode("utf-8"),
                {"Idempotency-Key": "k-retry"},
            )
            self.assertEqual(st, 504)
            self.assertFalse(data["success"])
            self.assertNotIn("/api/business_orders:k-retry", self.server.idempotency_store)

        # 2. Retry same key succeeds with business success=False -> HTTP 200 (existing contract)
        def biz_reject(service_name, service_type, request, timeout_sec=30):
            return {
                "ok": True,
                "timed_out": False,
                "returncode": 0,
                "stdout": "accepted=False, message='invalid station', mission_ids=[], queue_size=0",
                "stderr": "",
            }

        with mock.patch.object(rest_api_gateway, "service_call", side_effect=biz_reject):
            st2, data2 = self.post(
                "/api/business_orders",
                json.dumps({"business_order_id": "bo-t", "business_type": "campus_mail"}).encode("utf-8"),
                {"Idempotency-Key": "k-retry"},
            )
            self.assertEqual(st2, 200)
            self.assertFalse(data2["success"])

    def test_payload_too_large_returns_413(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.putrequest("POST", "/api/business_orders")
        conn.putheader("Content-Type", "application/json")
        conn.putheader("Content-Length", str(rest_api_gateway.MAX_BODY_BYTES + 1))
        conn.endheaders()
        resp = conn.getresponse()
        self.assertEqual(resp.status, 413)
        conn.close()


class ExternalCallbackOutboxTests(unittest.TestCase):
    def test_atomic_lock_idempotency_header_and_immediate_replay(self):
        received_headers = []
        fail_first = {"flag": True}

        class Receiver(BaseHTTPRequestHandler):
            def log_message(self, fmt, *args):
                return

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                _ = self.rfile.read(length)
                received_headers.append(self.headers.get("Idempotency-Key"))
                if fail_first["flag"]:
                    fail_first["flag"] = False
                    self.send_response(503)
                    self.end_headers()
                    self.wfile.write(b"temp err")
                    return
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"ok")

        srv = ThreadingHTTPServer(("127.0.0.1", 0), Receiver)
        port = srv.server_address[1]
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                outbox_path = Path(tmp) / "outbox.json"
                endpoint = f"http://127.0.0.1:{port}/cb"
                events = [{"stamp": "100", "mission_id": "m1", "state": "FINISHED",
                           "previous_state": "RUNNING", "message": "ok", "event_id": "100:m1:FINISHED"}]

                with external_callback_outbox.outbox_lock(outbox_path):
                    ob = external_callback_outbox.load_outbox(outbox_path)
                    external_callback_outbox.enqueue_events(ob, endpoint, events)
                    external_callback_outbox.save_outbox(outbox_path, ob)
                    # Persisted PENDING before delivery
                    pre = json.loads(outbox_path.read_text(encoding="utf-8"))
                    self.assertEqual(pre["100:m1:FINISHED"]["status"], "PENDING")

                    external_callback_outbox.send_pending(ob, endpoint, 5, replay_failed=False, outbox_path=outbox_path)
                    after_fail = json.loads(outbox_path.read_text(encoding="utf-8"))
                    self.assertEqual(after_fail["100:m1:FINISHED"]["status"], "FAILED")

                    # Immediate 0s replay with replay_failed=True
                    t0 = time.monotonic()
                    external_callback_outbox.send_pending(ob, endpoint, 5, replay_failed=True, outbox_path=outbox_path)
                    elapsed = time.monotonic() - t0
                    self.assertLess(elapsed, 2.0)
                    after_sent = json.loads(outbox_path.read_text(encoding="utf-8"))
                    self.assertEqual(after_sent["100:m1:FINISHED"]["status"], "SENT")

                self.assertEqual(received_headers, ["100:m1:FINISHED", "100:m1:FINISHED"])
                self.assertTrue(Path(str(outbox_path) + ".lock").exists())
                self.assertEqual(list(Path(tmp).glob("*.tmp")), [])
        finally:
            srv.shutdown()
            srv.server_close()
            t.join(timeout=2)

    def test_list_events_uses_json_dumps(self):
        captured = []

        def fake_run(args, timeout_sec=30):
            captured.append(args)
            return subprocess.CompletedProcess(args, 0, stdout="", stderr="")

        with mock.patch.object(external_callback_outbox, "run_ros2", side_effect=fake_run):
            external_callback_outbox.list_events(10, "FINISHED, extra: bad", "m1} injected: true")
        parsed = json.loads(captured[0][-1])
        self.assertEqual(parsed["state_filter"], "FINISHED, extra: bad")
        self.assertEqual(parsed["mission_id_filter"], "m1} injected: true")


class BridgeHardeningTests(unittest.TestCase):
    def test_json_dumps_prevents_yaml_injection_in_all_bridges(self):
        evil = "id1, priority: 999, order_type: hacked"
        req1 = json.loads(vda5050_file_bridge.build_submit_request(evil, 50, "{}"))
        self.assertEqual(req1["order_id"], evil)
        self.assertEqual(req1["order_type"], "vda5050")
        self.assertEqual(req1["priority"], 50)

        req2 = json.loads(facility_plc_file_adapter.build_facility_request(
            evil, "door_1, action: bypass", {"action": "open", "priority": 60}
        ))
        self.assertEqual(req2["request_id"], evil)
        self.assertEqual(req2["resource_id"], "door_1, action: bypass")
        self.assertEqual(req2["action"], "open")

        req3 = json.loads(vda5050_instant_action_bridge.workflow_request(evil, "cancel_active"))
        self.assertEqual(req3, {"order_id": evil, "cancel_active": True})

        captured = []
        with mock.patch.object(
            vda5050_mqtt_mock_bridge, "run_ros2",
            side_effect=lambda args: (captured.append(args), subprocess.CompletedProcess(args, 0, "", ""))[1],
        ):
            vda5050_mqtt_mock_bridge.submit_order({
                "order_id": evil,
                "pickup_node_id": "A",
                "dropoff_node_id": "B",
            })
        req4 = json.loads(captured[0][-1])
        self.assertEqual(req4["order_id"], evil)
        self.assertEqual(req4["order_type"], "vda5050")

    def test_bridges_catch_timeout_expired(self):
        for mod in (vda5050_file_bridge, facility_plc_file_adapter,
                    vda5050_mqtt_mock_bridge, vda5050_instant_action_bridge):
            with mock.patch.object(
                mod.subprocess, "run",
                side_effect=subprocess.TimeoutExpired(["ros2"], 30),
            ):
                res = mod.run_ros2(["ros2", "service", "call"])
                self.assertEqual(res.returncode, 124)

    def test_mqtt_bridge_blocks_path_traversal_and_symlinks(self):
        for bad in ("../escape", "a..b", "/etc/passwd", "bad/slash", ".hidden", ""):
            with self.assertRaises(ValueError):
                vda5050_mqtt_mock_bridge.validate_safe_id(bad, "order_id")

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            state_dir = root / "states"
            conn_dir = root / "connections"
            outside = root / "outside.json"
            outside.write_text("secret", encoding="utf-8")
            state_dir.mkdir()
            (state_dir / "ord_1.state.json").symlink_to(outside)

            with self.assertRaises(ValueError):
                vda5050_mqtt_mock_bridge.safe_output_path(state_dir, "ord_1.state.json")

            # Valid order writes atomically inside target dir
            order_file = root / "ord_ok.json"
            order_file.write_text(json.dumps({
                "order_id": "ord_ok-1.0",
                "serial_number": "robot_1",
                "pickup_node_id": "s1",
                "dropoff_node_id": "s2",
            }), encoding="utf-8")
            with mock.patch.object(
                vda5050_mqtt_mock_bridge, "run_ros2",
                return_value=subprocess.CompletedProcess(["ros2"], 0, "accepted=True, mission_id='m1', states=['FINISHED'], success=True", ""),
            ):
                ok = vda5050_mqtt_mock_bridge.process_order_file(order_file, state_dir, conn_dir, 5)
            self.assertTrue(ok)
            self.assertTrue((state_dir / "ord_ok-1.0.state.json").is_file())
            self.assertTrue((conn_dir / "robot_1.connection.json").is_file())
            self.assertEqual(list(state_dir.glob("*.tmp")), [])
            self.assertEqual(list(conn_dir.glob("*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
