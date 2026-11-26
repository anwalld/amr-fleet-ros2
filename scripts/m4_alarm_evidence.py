#!/usr/bin/env python3
"""阶段 7 Alarm 原始证据检查。"""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
from xml.etree import ElementTree

from m4_ops_preflight import rows

EXPECTED = {"alarm_sim_stop", "alarm_events_and_recovery", "alarm_installation"}


def verify(stage):
    source = stage / "alarm-evidence"
    records = rows(source / "results.tsv")
    cases = {row["case"]: row["result"] for row in records}
    issues = []
    if set(cases) != EXPECTED or len(records) != len(EXPECTED):
        issues.append("Alarm 用例缺项、重复或新增未登记用例")
    for case in sorted(EXPECTED):
        if cases.get(case) != "PASS":
            issues.append(f"{case} 未通过")
            continue
        log = (source / f"{case}.log").read_text(encoding="utf-8")
        if not log:
            issues.append(f"{case} 缺少原始日志")
        if case == "alarm_installation":
            metrics = {row["metric"]: row["value"] for row in rows(source / "installed_live.tsv")}
            if float(metrics.get("duration_s", 0)) <= 0 or int(metrics.get("snapshots", 0)) < 1:
                issues.append("安装后的实时窗口没有收到快照")
            for name in ("robot-console", "robot-console-replay"):
                if not (source / "desktop" / f"{name}.desktop").is_file():
                    issues.append(f"{name} 缺安装入口")
            continue
        try:
            xml = ElementTree.parse(source / f"{case}.xml")
        except ElementTree.ParseError as error:
            issues.append(f"{case} 的 QtTest XML 无法读取：{error}")
            continue
        tests = [test for test in xml.findall(".//testcase") if test.get("name") == case]
        if len(tests) != 1 or any(xml.findall(f".//{tag}") for tag in ("failure", "error", "skipped")):
            issues.append(f"{case} 缺少通过的 QtTest 断言")
        facts = [json.loads(value) for value in re.findall(r"OPS_EVIDENCE (\{.*\})", log)]
        evidence = [fact for fact in facts if fact.get("step") == case]
        if len(evidence) != 1:
            issues.append(f"{case} 缺少独立状态证据")
            continue
        item = evidence[0]
        if case == "alarm_sim_stop":
            requests = Counter(re.findall(r"console_service_request service=(\S+)", log))
            if (requests != {"/v2/set_fleet_sim_stop": 3, "/v2/submit_fleet_station_task": 1}
                    or not item.get("resumed") or not item.get("task")
                    or item.get("held_resources", 0) < 1 or item.get("stopped_ms", 0) < 1000):
                issues.append("Alarm 缺急停、路权保留或实际恢复运动证据")
        elif (item.get("events") != 2 or not item.get("recovered") or not item.get("map_retained")
              or not item.get("graph_requests", 0) or not item.get("stop_status_expired")):
            issues.append("Alarm 缺告警合并或重新获取路网证据")
    return {"cases": cases}, issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    args = parser.parse_args()
    try:
        details, issues = verify(args.stage)
    except (OSError, ValueError, KeyError, TypeError, ElementTree.ParseError) as error:
        details, issues = {}, [str(error)]
    print(json.dumps({"details": details, "issues": issues}, ensure_ascii=False))
    return int(bool(issues))


if __name__ == "__main__":
    sys.exit(main())
