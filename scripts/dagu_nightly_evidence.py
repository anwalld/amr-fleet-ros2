"""Check the evidence produced by a successful robot nightly runner."""

import csv
import hashlib
import json
import math
import os
import re
from pathlib import Path


from m4_ops_evidence import verify_terminal
from m4_alarm_evidence import verify as verify_alarm


MATRIX = {(str(robots), mode) for robots in (2, 4, 6) for mode in ("whole_route", "rolling")}
SCENARIOS = ("head_on", "ring3", "outside_wait")
RECORD_KINDS = {"graph", "pose", "snapshot", "traffic"}
SAMPLING_TOLERANCE = 5
MANIFEST_LINE = re.compile(r"^([0-9a-fA-F]{64})[ \t]+\*?(.+)$")


def find_source_manifest(output, workspace):
    candidates = []
    override = os.environ.get("ROBOT_SOURCE_MANIFEST", "").strip()
    if override:
        candidates.append(Path(override).expanduser())
    output_path = Path(output)
    workspace_path = Path(workspace)
    candidates.extend([
        output_path / "source.manifest",
        workspace_path / "source.manifest",
        workspace_path.parent / "control" / "source.manifest",
        workspace_path.parent / "source.manifest",
    ])
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(65536)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def verify_source_manifest(manifest_path, workspace):
    workspace_root = Path(workspace).resolve()
    manifest_file = Path(manifest_path)
    checked = 0
    missing = []
    mismatched = []
    invalid_lines = []
    issues = []

    for lineno, raw_line in enumerate(manifest_file.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = MANIFEST_LINE.fullmatch(line)
        if not match:
            invalid_lines.append(f"line {lineno}")
            continue
        expected_hash = match.group(1).lower()
        rel_path = match.group(2).strip()
        if rel_path.startswith("./"):
            rel_path = rel_path[2:]
        checked += 1
        target = (workspace_root / rel_path).resolve()
        try:
            target.relative_to(workspace_root)
        except ValueError:
            mismatched.append(f"{rel_path} (escapes workspace)")
            continue
        if not target.is_file():
            missing.append(rel_path)
            continue
        actual_hash = sha256_file(target)
        if actual_hash != expected_hash:
            mismatched.append(rel_path)

    if invalid_lines:
        issues.append(f"source.manifest 存在 {len(invalid_lines)} 行格式错误（示例：{', '.join(invalid_lines[:5])}）")
    if checked == 0 and not invalid_lines:
        issues.append("source.manifest 为空")
    if missing:
        issues.append(f"source.manifest 缺失 {len(missing)} 个文件（示例：{', '.join(missing[:5])}）")
    if mismatched:
        issues.append(f"source.manifest 有 {len(mismatched)} 个文件 SHA-256 失配（示例：{', '.join(mismatched[:5])}）")

    details = {
        "manifest": str(manifest_file),
        "checked_files": checked,
        "missing_count": len(missing),
        "missing_files": missing[:20],
        "mismatched_count": len(mismatched),
        "mismatched_files": mismatched[:20],
        "invalid_line_count": len(invalid_lines),
        "ok": not issues,
    }
    return details, issues


def rows(path, delimiter="\t"):
    with path.open(newline="") as stream:
        return list(csv.DictReader((line for line in stream if not line.startswith("#")), delimiter=delimiter))


def traffic_samples(output, minutes):
    stage = output / "stage5_traffic_stress"
    metrics = rows(stage / "metrics.tsv")
    issues = []
    details = {}
    if len(metrics) != len(MATRIX) or {(r["robots"], r["mode"]) for r in metrics} != MATRIX:
        issues.append("压测矩阵结果缺组、重复或包含未知组")
    expected = minutes * 60
    for robots, mode in sorted(MATRIX):
        name = f"stress_{robots}_{mode}"
        group = stage / name
        try:
            ts = [float(row["t_s"]) for row in rows(group / "state.csv", ",")]
            duration = float(json.loads((group / "summary.json").read_text())["duration_s"])
            if not math.isfinite(duration) or any(not math.isfinite(t) for t in ts):
                raise ValueError("采样时间含非有限值")
            gaps = [right - left for left, right in zip(ts, ts[1:])]
            largest = max(gaps, default=0)
            first, last = (ts[0], ts[-1]) if ts else (None, None)
            complete = (len(ts) >= expected - SAMPLING_TOLERANCE and ts
                        and min(gaps, default=0) >= 0
                        and 0 <= first <= SAMPLING_TOLERANCE and last >= expected - SAMPLING_TOLERANCE
                        and abs(duration - expected) <= SAMPLING_TOLERANCE and largest <= SAMPLING_TOLERANCE)
            details[name] = {"samples": len(ts), "first_sample_seconds": first, "last_sample_seconds": last,
                             "largest_gap_seconds": round(largest, 3), "duration_seconds": duration}
            if not complete:
                issues.append(f"{name} 采样窗口不完整（{len(ts)} 条，最大间隔 {largest:.2f} 秒）")
        except (OSError, ValueError, KeyError, TypeError) as error:
            issues.append(f"{name} 采样证据不可读：{error}")
    return details, issues


def shared_acceptance(output, workspace, rounds):
    issues = []
    stage = output / "stage3_acceptance/acceptance"
    planned = rows(workspace / "scripts/acceptance_cases.tsv")
    actual = rows(stage / "results.tsv")
    expected = {(r["suite"], r["case"]): r["stack"] for r in planned}
    actual_stacks = {(r["suite"], r["case"]): r["kind"] for r in actual}
    if not planned or len(planned) != len(expected) or len(actual) != len(expected) or actual_stacks != expected or any(r["status"] != "PASS" for r in actual):
        issues.append("全量验收未完整匹配登记表或存在失败")
    mock = [key for key, stack in expected.items() if stack == "mock_bringup"]
    for suite, case in mock:
        log = (stage / f"_stack__{suite}__{case}.log").read_text()
        if not any("[runner] stack=mock_bringup " in line and " ready=1 " in line for line in log.splitlines()):
            issues.append(f"{suite}:{case} 缺 mock_bringup 就绪证据")
    for case in ("whole_route_retry_wait", "deadlock_pause_clear"):
        if actual_stacks.get(("traffic", case)) != "core":
            issues.append(f"traffic:{case} 缺 core 栈证据")
    if "robot_beta removed from waiting queue" not in (stage / "traffic__block_replan.log").read_text():
        issues.append("缺封路移出等待队列证据")
    business = (stage / "business__scenario_workflow_shapes.log").read_text().splitlines()
    templates = {line for line in business if line.startswith("ok: ") and "/" in line and " accepted = True" in line}
    if len(templates) != 12 or "scenario workflow shapes passed" not in business:
        issues.append("缺 12 个模板受理或场景执行证据")

    stage2 = (output / "stage2_unit_test/test_result.txt").read_text()
    if any(f"build/{package}/" not in stage2 for package in ("robot_console", "robot_console_core")):
        issues.append("阶段 2 缺少两个调度台包的测试证据")
    stage6 = output / "stage6_deadlock_scenarios"
    scenarios = rows(stage6 / "deadlock_scenarios.tsv")
    expected_scenarios = {(scenario, str(i)) for scenario in SCENARIOS for i in range(1, rounds + 1)}
    if (len(scenarios) != len(expected_scenarios)
            or {(r["scenario"], r["round"]) for r in scenarios} != expected_scenarios
            or any(r["verdict"] != "PASS" for r in scenarios)):
        issues.append("死锁场景缺失、重复或失败")
    recordings = [stage6 / f"scenario_{scenario}_{round_id}.console.jsonl" for scenario, round_id in sorted(expected_scenarios)]
    recordings.append(output / "stage5_traffic_stress/stress_6_rolling/console.jsonl")
    for recording in recordings:
        with recording.open() as stream:
            kinds = {json.loads(line)["kind"] for line in stream}
        if not RECORD_KINDS <= kinds:
            issues.append(f"{recording.name} 缺录像记录类型")
    smoke = {r["metric"]: r["value"] for r in rows(output / "stage7_console_smoke/console_smoke.tsv")}
    if smoke.get("verdict") != "PASS" or not smoke.get("replay_check", "").startswith("OK"):
        issues.append("调度台冒烟或回放未通过")
    return {"cases": len(actual), "mock_cases": len(mock), "templates": len(templates),
            "scenarios": len(scenarios), "recordings": len(recordings), "smoke": smoke}, issues


def verify_task_control_restart(directory):
    scenarios = {
        "d16": "d16 first start and repeated checkpoint verified",
        "fleet": "fleet first start and repeated checkpoint verified",
        "dispatcher-state": "dispatcher: original assignment, persisted and live receipts recovered; motion writes blocked",
        "d16-state": "D16: original live UUID and pause rechecked, pending queue recovered, dispatch held",
    }
    directory /= "task-control-restart"
    actual = rows(directory / "results.tsv")
    expected = [{"case": name, "status": "PASS"} for name in scenarios]
    issues = []
    if actual != expected:
        issues.append("任务控制重启结果缺项、重复或失败")
    for name, assertion in scenarios.items():
        if assertion not in (directory / f"result-{name}.log").read_text().splitlines():
            issues.append(f"{name} 缺重启断言")
    return {"results": actual}, issues


def verify_task_control_scenarios(directory):
    from verify_task_control_trace import verify_trace

    directory /= "task-control-scenarios"
    scenarios = {
        "d16-delayed-reply": "同目标同修订重试；旧暂停回执迟到未覆盖恢复",
        "d16-route-hazard": "定位丢失期间路权已授予但没有发导航目标；定位恢复后继续",
        "d16-motion-cutover": "暂停位移与采样连续性通过；取消后下一单仅在明确放行后运动",
        **dict.fromkeys(
            ("fleet-rolling", "fleet-whole-route", "fleet-cancel-paused"),
            "pause, fleet-stop rejection, replay, cancellation, route retention, explicit move and stale target passed",
        ),
        "fleet-restart-held": "restarted canceled hold: original receipt, pose and live route verified",
        "fleet-restart-missing-route": "missing route: same checkpoint kept motion inhibited",
        "fleet-restart-paused-rolling": "restarted paused task: same execution, pose and route retained",
        "fleet-restart-paused-whole-route": "restarted paused task: same execution, pose and route retained",
    }
    actual = rows(directory / "results.tsv")
    expected = [{"case": name, "status": "PASS"} for name in scenarios]
    issues = []
    if actual != expected:
        issues.append("任务控制仿真结果缺项、重复或失败")
    for name, assertion in scenarios.items():
        if assertion not in (directory / f"{name}.log").read_text().splitlines():
            issues.append(f"{name} 缺少仿真断言")
    traces = {}
    for name in scenarios:
        try:
            traces[name] = verify_trace(name, directory / f"{name}-trace.jsonl")
        except (OSError, ValueError, KeyError) as error:
            issues.append(str(error))
    return {"results": actual, "traces": traces}, issues


def verify_evidence(output, workspace, stages, minutes, rounds, profile):
    result = {"profile": profile or "nightly", "checks": {}, "issues": []}
    steps = []
    manifest_path = find_source_manifest(output, workspace)
    if manifest_path is not None:
        steps.append(("source_manifest", verify_source_manifest, (manifest_path, workspace)))
    if 5 in stages:
        steps.append(("traffic_samples", traffic_samples, (output, minutes)))
    if profile == "shared-rework":
        steps.append(("shared_acceptance", shared_acceptance, (output, workspace, rounds)))
        steps.append(("m4_ops_stage7", verify_terminal, (output / "stage7_console_smoke", workspace)))
        steps.append(("m4_alarm_stage7", verify_alarm, (output / "stage7_console_smoke",)))
        steps.append(("task_control_restart", verify_task_control_restart, (output / "stage7_console_smoke",)))
        steps.append(("task_control_scenarios", verify_task_control_scenarios, (output / "stage7_console_smoke",)))
    for name, check, args in steps:
        try:
            details, issues = check(*args)
            result["checks"][name] = details
            result["issues"].extend(issues)
        except (OSError, ValueError, KeyError, TypeError) as error:
            result["issues"].append(f"{name} 验收证据不可读：{error}")
    return result
