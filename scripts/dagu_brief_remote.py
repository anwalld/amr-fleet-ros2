"""Read one registered nightly on the VM. Invoked via ssh python3 -."""
import csv
import hashlib
import json
import re
import sys
from datetime import datetime
from pathlib import Path


def rows(path):
    if not path.is_file():
        return []
    with path.open() as stream:
        return list(csv.DictReader((s for s in stream if not s.startswith("#")), delimiter="\t"))


def native_terminal(workspace, plan):
    # Dagu 2.27's run store for the workspace-local Dagu configuration (control/dagu or control/state).
    if not re.fullmatch(r"[A-Za-z0-9_-]+", plan["run_id"]):
        return None
    stores = ("control/dagu", "control/state")
    records = []
    for path in (p for s in stores for p in (workspace.parent / s / "data/dag-runs/robot-nightly/dag-runs").glob(
            f"*/*/*/dag-run_*_{plan['run_id']}/a_*/status.jsonl")):
        with path.open() as stream:
            last_complete = None
            for line in stream:
                if line.endswith("\n") and line.strip():
                    last_complete = line
        if last_complete is None:
            continue
        try:
            native = json.loads(last_complete)
        except json.JSONDecodeError:
            continue
        if not isinstance(native, dict):
            continue
        params = native.get("paramsList", [])
        if (native.get("name") == "robot-nightly"
                and native.get("dagRunId") == plan["run_id"]
                and f"ROBOT_WORKSPACE={workspace}" in params
                and f"NIGHTLY_REV={plan['revision']}" in params):
            records.append(native)
    if not records:
        return None
    # A later retry can still be active even though an earlier attempt failed.
    latest = max(records, key=lambda native: native.get("createdAt", 0))
    state = {
        2: "failed",
        3: "aborted",
        4: "dry_run_passed" if plan.get("dry_run") else "passed",
    }.get(latest.get("status"))
    finished = latest.get("finishedAt")
    started = latest.get("startedAt")
    if state and started and finished:
        planned_at = datetime.fromisoformat(plan["checked_at"])
        if abs((datetime.fromisoformat(started) - planned_at).total_seconds()) <= 60:
            return state, finished
    return None


def read_run(output, workspace, action, expected):
    path = output / "checks.jsonl"
    if output.resolve() != output or path.is_symlink():
        raise ValueError("unexpected run path")
    entries = []
    with path.open(errors="replace") as stream:
        for line in stream:
            if not line.endswith("\n") or not line.strip():
                continue
            try:
                parsed = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(parsed, dict):
                entries.append(parsed)
    if not entries:
        raise ValueError("no valid entries in checks.jsonl")
    plan = next((e for e in entries if e.get("state") == "planned"), None)
    if plan is None:
        last = entries[-1]
        state = last.get("state", "unknown")
        event = last.get("checked_at", "")
        run_id = last.get("run_id") or re.sub(r"^\d{4}-\d{2}-\d{2}-", "", output.name)
        identity = hashlib.sha256(json.dumps(entries[0], sort_keys=True).encode()).hexdigest()
        if expected != "-" and expected != identity:
            raise ValueError("run identity changed")
        terminal = state == "blocked"
        if action == "clean":
            if not terminal or expected == "-":
                raise ValueError("cleanup requires a bound terminal record")
            path.unlink()
            return {"removed": str(path)}
        if state == "blocked":
            reason = last.get("reason") or ("缺陷账本触发升级" if last.get("escalate") else "前置检查阻塞")
            brief = f"{run_id} 已阻塞（{reason}），已结束。"
        else:
            brief = f"{run_id} 状态 {state}，缺少计划记录。"
        return dict(run_id=run_id, identity=identity, event=event, terminal=terminal, brief=brief)
    if plan.get("output_dir") != str(output):
        raise ValueError("output directory differs from plan")
    identity = hashlib.sha256(json.dumps(plan, sort_keys=True).encode()).hexdigest()
    if expected != "-" and expected != identity:
        raise ValueError("run identity changed")
    last = entries[-1]
    state, event = last.get("state", "running"), last.get("checked_at", "")
    terminal = "exit_code" in last and state in {"failed", "passed", "dry_run_passed"}
    if not terminal:
        status = native_terminal(workspace, plan)
        if status is not None:
            state, event = status
            terminal = True
    if action == "clean":
        if not terminal or expected == "-":
            raise ValueError("cleanup requires a bound terminal record")
        path.unlink()
        return {"removed": str(path)}
    counts = []
    passed = finished = 0
    for n in plan["stages"]:
        directory = next(iter(output.glob(f"stage{n}_*")), None)
        if not directory:
            continue
        verdict_file = directory / "verdict"
        verdict = verdict_file.read_text().strip() if verdict_file.exists() else ""
        finished += bool(verdict)
        passed += verdict == "PASS"
        count = None
        summary_file = directory / "summary.md"
        summary = summary_file.read_text() if summary_file.exists() else ""
        if n == 1:
            match = re.search(r'(\d+) packages? finished', summary)
            if match and verdict == "PASS":
                count = f"{match[1]}/{match[1]} 包"
        elif n == 2:
            match = re.search(r'(\d+) tests?, (\d+) errors?, (\d+) failures?, (\d+) skipped', summary)
            if match:
                total, *missed = map(int, match.groups())
                count = f"{total-sum(missed)}/{total} 项"
        elif n == 3:
            results = rows(directory / "acceptance/results.tsv")
            total = len(rows(workspace / "scripts/acceptance_cases.tsv"))
            if total:
                count = f"{sum(r.get('status') == 'PASS' for r in results)}/{total} 项"
        elif n == 4 and (directory / "demo_verdict.tsv").exists():
            checks = (directory / "demo_verdict.tsv").read_text().splitlines()
            count = f"{sum(s.endswith(chr(9)+'OK') for s in checks)}/{len(checks)} 项"
        elif n == 5 and verdict == "PASS":
            count = "6/6 组"
        elif n == 6:
            checks = rows(directory / "deadlock_scenarios.tsv")
            count = f"{sum(r.get('verdict') == 'PASS' for r in checks)}/{3*plan['scenario_rounds']} 遍"
        elif n == 7:
            parts = []
            for suite, file, field, expected_count in (("Ops","console_ops_results.tsv","status",10),
                                                 ("Alarm","alarm-evidence/results.tsv","result",3)):
                checks = rows(directory / file)
                if checks:
                    parts.append(f"{suite} {sum(r.get(field) == 'PASS' for r in checks)}/{expected_count}")
            count = "、".join(parts)
        label = ["编译","单测","验收","演示","压测","死锁","调度台","报告"][n-1]
        unfinished = "未完成" if terminal else "进行中"
        fallback = {"PASS": "1/1", "FAIL": "0/1"}.get(verdict, verdict or unfinished)
        counts.append(label + " " + (count or fallback))
    total = len(plan["stages"])
    eta = last.get("eta")
    timing = "已结束" if terminal else ("预计 " + eta[5:16].replace("T"," ") + "（北京时间）" if eta else "ETA 待定")
    state = {"failed":"失败", "aborted":"已取消", "passed":"通过", "dry_run_passed":"预检通过"}.get(state, "运行中")
    brief = f"{plan['run_id']} {state}，完成 {finished}/{total} 阶段，通过 {passed}/{total}，{timing}。" + "，".join(counts) + "。"
    return dict(run_id=plan["run_id"], identity=identity, event=event, terminal=terminal, brief=brief)


if __name__ == "__main__":
    print(json.dumps(read_run(Path(sys.argv[1]), Path(sys.argv[2]), *sys.argv[3:])))
