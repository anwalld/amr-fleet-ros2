#!/usr/bin/env python3
"""Run and observe one robot nightly job from Dagu."""

import json
import os
import re
import signal
import stat
import statistics
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

from dagu_nightly_evidence import verify_evidence

ROOT = Path(__file__).resolve().parents[1]
BEIJING = timezone(timedelta(hours=8), "Asia/Shanghai")
CHECK_INTERVAL = 30 * 60
ETA_GRACE = 15 * 60
ETA_FALLBACK = 5 * 60
STAGES = dict(enumerate(
    ("build", "unit_test", "acceptance", "demo", "traffic_stress",
     "deadlock_scenarios", "console_smoke", "report"), 1
))
REPORT_ROW = re.compile(r"^\| (\d+) [^|]+\| PASS \| (\d+)s \|([^|]*)", re.M)
STAGE_START = re.compile(r"^\[([^]]+)\] stage (\d+) \S+ start \(timeout (\d+)s\)$", re.M)
FALLBACKS = {7: (420, "5 分钟采样加 2 分钟启动和清理"), 8: (10, "报告生成预留 10 秒")}


def timestamp():
    return datetime.now(BEIJING)


def read_text(path):
    try:
        return path.read_text(errors="replace")
    except FileNotFoundError:
        return ""


def stage_budgets(stages, stress_minutes, scenario_rounds):
    samples = {stage: [] for stage in stages}
    for report in sorted((ROOT / "docs/baseline/nightly").glob("*.md")):
        for number, duration, summary in REPORT_ROW.findall(read_text(report)):
            stage, seconds = int(number), int(duration)
            if stage not in samples:
                continue
            if stage == 5:
                baseline = re.search(r"每组 (\d+) 分钟", summary)
                if not baseline:
                    continue
                seconds = max(seconds - 360 * int(baseline[1]), 0) + 360 * stress_minutes
            if stage == 6:
                baseline = re.search(r"PASS \d+/(\d+) 遍", summary)
                if not baseline or int(baseline[1]) == 0:
                    continue
                seconds = round(seconds * (3 * scenario_rounds) / int(baseline[1]))
            samples[stage].append((seconds, report.name))
    budgets = {}
    for stage, values in samples.items():
        if values:
            budgets[stage] = {
                "seconds": round(statistics.median(duration for duration, _ in values)),
                "source": [name for _, name in values],
            }
        else:
            seconds, reason = FALLBACKS.get(stage, (None, "无适用历史样本"))
            budgets[stage] = {"seconds": seconds, "source": reason}
    return budgets


def stage_dir(output, stage):
    return output / f"stage{stage}_{STAGES[stage]}"


def estimate(now, pending, budgets, active=None, elapsed=0, stress_rate=None, dry=False):
    if dry:
        return {"eta": None, "eta_reason": "dry-run"}
    remaining = 0
    for stage in pending:
        planned = budgets[stage]["seconds"]
        if planned is None:
            return {"eta": None, "eta_reason": f"阶段 {stage} 缺少估时依据"}
        if stage == active:
            if stage == 5 and stress_rate is not None:
                planned = round(stress_rate * 6)
            if elapsed >= planned:
                return {"eta": None, "eta_reason": f"阶段 {stage} 达到估计用时，等待进展证据"}
            planned -= elapsed
        remaining += planned
    return {"eta": (now + timedelta(seconds=round(remaining))).isoformat(),
            "remaining_seconds": round(remaining)}


def observe(output, stages, budgets, dry, child):
    now = timestamp()
    exit_code = child.poll()
    verdicts = {}
    for stage in stages:
        directory = stage_dir(output, stage)
        verdict = read_text(directory / "verdict").strip()
        seconds = read_text(directory / "seconds").strip()
        if verdict and seconds.isdecimal():
            verdicts[stage] = {"verdict": verdict, "seconds": int(seconds)}

    pending = [stage for stage in stages if stage not in verdicts]
    snapshot = {"pid": child.pid, "stages": verdicts, "pending_stages": pending}
    if exit_code is not None:
        report = output / "report.md"
        report_written = report.is_file() and report.stat().st_size > 0
        accepted = {"PASS", "SKIP"} if dry else {"PASS"}
        issues = [f"阶段 {stage} 为 {result['verdict']}" for stage, result in verdicts.items()
                  if result["verdict"] not in accepted]
        if pending:
            issues.append(f"缺阶段结论 {pending}")
        if not report_written:
            issues.append("报告缺失或为空")
        if exit_code:
            issues.append(f"进程退出码 {exit_code}")
        if issues:
            state = "failed"
        else:
            state = "dry_run_passed" if dry else "passed"
        return {**snapshot, "state": state, "exit_code": exit_code, "issues": issues,
                "report_written": report_written, "next_check_at": None}

    starts = STAGE_START.findall(read_text(output / "nightly.log"))
    active, elapsed, stress_rate = None, 0, None
    state = "running"
    if starts:
        started, number, limit = starts[-1]
        stage = int(number)
        if stage in pending:
            active = stage
            started_at = datetime.fromisoformat(started).replace(tzinfo=BEIJING)
            elapsed = max(0, int((now - started_at).total_seconds()))
            directory = stage_dir(output, stage)
            if active == 5:
                metrics = directory / "metrics.tsv"
                lines = read_text(metrics).splitlines(keepends=True)
                groups = sum(line.endswith("\n") for line in lines[1:])
                if 0 < groups < 6:
                    stress_rate = max(1, metrics.stat().st_mtime - started_at.timestamp()) / groups
            latest, mtime = latest_evidence(output, directory)
            evidence_at = datetime.fromtimestamp(mtime, BEIJING)
            if elapsed >= int(limit):
                state = "timeout_expected"
            elif (now - evidence_at).total_seconds() >= CHECK_INTERVAL:
                state = "no_recent_output"
            snapshot.update(active_stage=stage, elapsed_seconds=elapsed, stage_timeout_seconds=int(limit),
                            last_evidence=str(latest), evidence_at=evidence_at.isoformat())
            if active == 5:
                snapshot["stress_groups"] = {"completed": groups, "total": 6}
    if any(result["verdict"] not in {"PASS", "SKIP"} for result in verdicts.values()):
        snapshot["stage_failure_seen"] = True
    if not pending:
        timing = {"eta": None, "eta_reason": "所选阶段已结束，等待进程退出"}
    else:
        timing = estimate(now, pending, budgets, active, elapsed, stress_rate, dry=dry)
    return {**snapshot, "state": state, **timing}


def latest_evidence(output, directory):
    latest = output / "nightly.log"
    mtime = latest.stat().st_mtime
    for path in directory.rglob("*"):
        try:
            info = path.stat()
        except FileNotFoundError:
            continue
        if stat.S_ISREG(info.st_mode) and info.st_mtime > mtime:
            latest, mtime = path, info.st_mtime
    return latest, mtime


def record(output, **values):
    entry = json.dumps({"checked_at": timestamp().isoformat(), **values}, ensure_ascii=False)
    with (output / "checks.jsonl").open("a") as stream:
        stream.write(entry + "\n")
    print(entry, flush=True)


def predicted_time(snapshot):
    eta = snapshot.get("eta")
    return datetime.fromisoformat(eta) if eta else None


def diagnostics(output, snapshot):
    stage = snapshot.get("active_stage")
    logs = [output / "nightly.log"]
    if stage is not None:
        logs.append(stage_dir(output, stage) / "stage.log")
    for number, result in snapshot["stages"].items():
        if result["verdict"] not in {"PASS", "SKIP"}:
            logs.append(stage_dir(output, number) / "summary.md")
    tails = {}
    for path in logs:
        try:
            with path.open("rb") as stream:
                stream.seek(max(0, path.stat().st_size - 8192))
                tails[str(path)] = stream.read().decode(errors="replace").splitlines()[-12:]
        except FileNotFoundError:
            continue
    return {"observed_state": snapshot["state"], "logs": tails}


def watch(output, stages, budgets, dry, child, initial, stress_minutes, scenario_rounds, profile):
    eta_check = predicted_time(initial)
    next_interval = time.monotonic()
    previous_progress = None
    delayed = False
    while True:
        now = timestamp()
        monotonic = time.monotonic()
        interval_due = monotonic >= next_interval
        eta_due = eta_check is not None and now >= eta_check
        if child.poll() is not None or interval_due or eta_due:
            snapshot = observe(output, stages, budgets, dry, child)
            if "exit_code" in snapshot:
                if snapshot["state"] == "failed":
                    snapshot["diagnostics"] = diagnostics(output, snapshot)
                if snapshot["state"] == "passed":
                    verdict = verify_evidence(output, ROOT, stages, stress_minutes, scenario_rounds, profile)
                    (output / "postcheck.json").write_text(json.dumps(verdict, ensure_ascii=False, indent=2) + "\n")
                    snapshot["postcheck"] = "postcheck.json"
                    snapshot["issues"].extend(verdict["issues"])
                    if verdict["issues"]:
                        snapshot["state"] = "failed"
                record(output, **snapshot, next_eta_check_at=None)
                return int(snapshot["state"] == "failed")

            if interval_due:
                next_interval += (int((monotonic - next_interval) / CHECK_INTERVAL) + 1) * CHECK_INTERVAL
            progress = (tuple(snapshot["stages"]), snapshot.get("stress_groups", {}).get("completed"))
            updated_eta = predicted_time(snapshot)
            decision = {"trigger": "eta" if eta_due else "interval"}
            if eta_due:
                decision["due_eta"] = eta_check.isoformat()
                current = timestamp()
                remaining = (updated_eta - current).total_seconds() if updated_eta else ETA_FALLBACK
                healthy = (snapshot["state"] == "running" and not snapshot.get("stage_failure_seen")
                           and (snapshot.get("last_evidence") is not None or not snapshot["pending_stages"]))
                if healthy and not delayed and 0 < remaining <= ETA_GRACE and current <= eta_check + timedelta(seconds=ETA_GRACE):
                    eta_check = updated_eta or current + timedelta(seconds=ETA_FALLBACK)
                    delayed = True
                    decision["action"] = "reschedule_eta"
                    decision["eta"] = eta_check.isoformat()
                    decision["remaining_seconds"] = round(remaining)
                    if updated_eta is None:
                        decision["eta_reason"] = "进度仍在更新，先顺延一次复查"
                else:
                    eta_check = None
                    reason = "顺延后仍未完成" if delayed else "进度异常或偏差超出顺延范围"
                    delayed = True
                    decision.update(action="diagnose", diagnosis_reason=reason,
                                    diagnostics=diagnostics(output, snapshot))
            elif previous_progress is not None and progress != previous_progress and updated_eta:
                eta_check = updated_eta
                delayed = False
            previous_progress = progress

            interval_wait = max(0, next_interval - time.monotonic())
            interval_at = timestamp() + timedelta(seconds=interval_wait)
            decision["next_eta_check_at"] = eta_check.isoformat() if eta_check else None
            decision["next_interval_check_at"] = interval_at.isoformat()
            decision["next_check_at"] = min(interval_at, eta_check).isoformat() if eta_check else interval_at.isoformat()
            record(output, **{**snapshot, **decision})

        wait = max(0, next_interval - time.monotonic())
        if eta_check is not None:
            # Recheck the wall clock after suspend or a clock correction.
            wait = min(wait, max(0, (eta_check - timestamp()).total_seconds()), 60)
        try:
            child.wait(timeout=wait)
        except subprocess.TimeoutExpired:
            continue


def check_defects(output, ledger):
    if not ledger:
        return True
    result = subprocess.run(
        [sys.executable, "-B", str(ROOT / "tools/robot_guard.py"), "check-defects", ledger],
        cwd=ROOT, capture_output=True, text=True,
    )
    if result.returncode:
        record(output, state="blocked", ledger=ledger, reason=result.stderr.strip())
        return False
    counts = json.loads(result.stdout)
    escalate = any(details["escalate"] for details in counts.values())
    record(output, state="blocked" if escalate else "defects_checked", ledger=ledger,
           defect_counts=counts, escalate=escalate)
    return not escalate


def run():
    run_id = os.environ.get("DAG_RUN_ID", "")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", run_id):
        raise ValueError("需要 Dagu 提供合法的 DAG_RUN_ID")
    dry_setting = os.environ.get("DRY_RUN", "1")
    if dry_setting not in {"0", "1"}:
        raise ValueError("DRY_RUN 必须为 0 或 1")
    dry = dry_setting == "1"
    if not dry and sys.platform != "linux":
        raise ValueError("真实夜跑需要 Ubuntu ROS 测试主机")
    selected = os.environ.get("ONLY_STAGES", "").split()
    if selected and (set(selected) - set(map(str, STAGES)) or "8" not in selected):
        raise ValueError("ONLY_STAGES 仅支持 1–8，且须包含报告阶段 8")
    stages = [stage for stage in STAGES if not selected or str(stage) in selected]
    stress_minutes = int(os.environ.get("STRESS_MINUTES", "30"))
    scenario_rounds = int(os.environ.get("SCENARIO_ROUNDS", "3"))
    if stress_minutes < 1 or scenario_rounds < 1:
        raise ValueError("STRESS_MINUTES 和 SCENARIO_ROUNDS 必须为正整数")
    revision = os.environ.get("NIGHTLY_REV") or ("dry-run" if dry else "")
    if not revision:
        raise ValueError("真实夜跑须明确设置 NIGHTLY_REV")
    profile = os.environ.get("ROBOT_POSTCHECK", "")
    if profile not in {"", "shared-rework"} or (profile and stages != list(STAGES)):
        raise ValueError("shared-rework 验收仅适用于完整夜跑")

    default_home = Path.home() / ("nightly-dry-run" if dry else "nightly")
    run_home = Path(os.environ.get("ROBOT_NIGHTLY_HOME") or default_home).expanduser().resolve()
    output = run_home / f"{timestamp():%Y-%m-%d}-{run_id}"
    output.mkdir(parents=True)
    if not check_defects(output, os.environ.get("ROBOT_DEFECT_LEDGER")):
        return 1

    budgets = stage_budgets(stages, stress_minutes, scenario_rounds)
    initial = estimate(timestamp(), stages, budgets, dry=dry)
    record(output, state="planned", run_id=run_id, revision=revision, timezone="Asia/Shanghai",
           output_dir=str(output), log=str(output / "nightly.log"), dry_run=dry, stages=stages,
           stress_minutes=stress_minutes, scenario_rounds=scenario_rounds, budgets=budgets,
           next_eta_check_at=initial["eta"], postcheck_profile=profile or "nightly",
           completion="进程成功退出、报告非空、所选阶段通过（dry-run 允许 SKIP）",
           **initial)
    env = {**os.environ, "TZ": "Asia/Shanghai", "DRY_RUN": dry_setting,
           "NIGHTLY_REV": revision, "ONLY_STAGES": " ".join(map(str, stages))}
    child = subprocess.Popen(
        ["bash", str(ROOT / "scripts/nightly.sh"), str(output)],
        cwd=ROOT, env=env, start_new_session=True,
    )

    def forward_signal(signum, _frame):
        if child.poll() is None:
            child.send_signal(signum)

    signal.signal(signal.SIGINT, forward_signal)
    signal.signal(signal.SIGTERM, forward_signal)
    try:
        return watch(output, stages, budgets, dry, child, initial, stress_minutes, scenario_rounds, profile)
    finally:
        if child.poll() is None:
            child.terminate()
            child.wait()


if __name__ == "__main__":
    try:
        sys.exit(run())
    except (OSError, ValueError) as error:
        sys.exit(f"robot-nightly: {error}")
