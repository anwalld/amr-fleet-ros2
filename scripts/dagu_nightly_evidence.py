"""Check the evidence produced by a successful robot nightly runner."""

import csv
import json
import math


MATRIX = {(str(robots), mode) for robots in (2, 4, 6) for mode in ("whole_route", "rolling")}
SCENARIOS = ("head_on", "ring3", "outside_wait")
RECORD_KINDS = {"graph", "pose", "snapshot", "traffic"}
SAMPLING_TOLERANCE = 5


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


def verify_evidence(output, workspace, stages, minutes, rounds, profile):
    result = {"profile": profile or "nightly", "checks": {}, "issues": []}
    steps = []
    if 5 in stages:
        steps.append(("traffic_samples", traffic_samples, (output, minutes)))
    if profile == "shared-rework":
        steps.append(("shared_acceptance", shared_acceptance, (output, workspace, rounds)))
    for name, check, args in steps:
        try:
            details, issues = check(*args)
            result["checks"][name] = details
            result["issues"].extend(issues)
        except (OSError, ValueError, KeyError, TypeError) as error:
            result["issues"].append(f"{name} 验收证据不可读：{error}")
    return result
