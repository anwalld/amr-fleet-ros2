#!/usr/bin/env python3
"""核对阶段 7 的独立结果、QtTest 和服务日志。可由夜跑与 Dagu 共用。"""

import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import sys
from xml.etree import ElementTree

from acceptance_compare import current_results
from m4_ops_preflight import keyed, rows
from m4_ops_smoke_boundary import inspect

# 每项业务观测保留在对应 QtTest 的原始输出，记录前须通过该项业务断言。
OBSERVATIONS = {
    "workflow_submit": ("workflow_observed", {"state": "QUEUED"}, {"children"}),
    "workflow_control": ("workflow_control_observed",
                         {"paused_first": True, "paused_after_resume": False, "active_terminal": "CANCELED"},
                         {"active", "canceled", "final_state"}),
    "fleet_submit_remote": ("remote_task_observed", {"robot": "robot_1", "state": "FINISHED"}, set()),
    "fleet_submit_local": ("local_task_observed", {"robot": "robot_1", "state": "FINISHED"}, {"mission_id"}),
    "route_block": ("route_observed", {"occupied_owner": "alpha"}, {"rejection", "external_refresh_ms"}),
    "fleet_enabled": ("fleet_flags_observed", {"sequence": [False, True]}, {"state", "voltage"}),
    "clear_match": ("clear_match_observed", {"new_owner": "beta", "resume": ["alpha", "beta"]}, {"released"}),
    "clear_owner_conflict": ("clear_owner_conflict_observed", {"previous": "alpha", "current": "beta"}, set()),
    "clear_scope_conflict": ("clear_scope_conflict_observed", {"resource_change": True, "resume_change": True}, set()),
    "submit_timeout": ("timeout_observed", {"requests": 1, "map_refreshed": True}, {"elapsed_ms", "snapshots"}),
}
REQUESTS = {
    "workflow_submit": {"list_scenario_workflows": 1, "submit_order": 1, "get_workflow_status": 1},
    "workflow_control": {"list_scenario_workflows": 1, "submit_order": 1, "get_workflow_status": 4,
                         "pause_workflow": 1, "resume_workflow": 1, "cancel_workflow": 1},
    "fleet_submit_remote": {"submit_fleet_station_task": 1},
    "fleet_submit_local": {"submit_fleet_station_task": 1},
    "route_block": {"block_station_route": 2, "unblock_station_route": 2},
    "fleet_enabled": {"update_fleet_robot_state": 2},
    "clear_match": {"preview_traffic_reservation_clear": 1, "clear_traffic_reservation_if_matches": 1},
    "clear_owner_conflict": {"preview_traffic_reservation_clear": 2, "clear_traffic_reservation_if_matches": 1},
    "clear_scope_conflict": {"preview_traffic_reservation_clear": 4, "clear_traffic_reservation_if_matches": 2},
    "submit_timeout": {"submit_fleet_station_task": 1},
}
REQUEST_LINE = re.compile(r"console_service_request service=/v2/(\w+) operation_id=(\d+) target=(\S*) include_active=(\w+)")


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def run_metadata(path):
    with path.open(encoding="utf-8", newline="") as stream:
        pairs = list(csv.reader(stream, delimiter="\t"))
    result = dict(pairs)
    require(len(result) == len(pairs), "运行元数据重复")
    return result


def check_case(stage, name, kind):
    prefix = stage / "ops-evidence" / name
    metadata = run_metadata(prefix / "run.tsv")
    domain = int(metadata.get("ros_domain_id", -1))
    require(metadata.get("case") == name and metadata.get("kind") == kind and
            metadata.get("exit_code") == "0" and metadata.get("revision") and 0 <= domain < 230,
            "运行元数据不完整或退出码非 0")
    require((prefix / "stack.log").stat().st_size > 0, "仿真栈日志为空")
    xml = ElementTree.parse(prefix / "test.xml")
    tests = [test for test in xml.findall(".//testcase") if test.get("name") == name]
    require(len(tests) == 1 and not any(xml.findall(f".//{tag}") for tag in ("failure", "error", "skipped")),
            "QtTest 结果缺失、失败或跳过")
    log = (prefix / "console.log").read_text(encoding="utf-8")
    records = [json.loads(data) for data in re.findall(r"OPS_EVIDENCE (\{.*\})", log)]
    step, expected_fields, required_fields = OBSERVATIONS[name]
    observed = [record for record in records if record.get("step") == step]
    require(len(observed) == 1 and observed[0].get("target"), "缺少独立业务状态观测")
    observation = observed[0]
    for field, expected in expected_fields.items():
        require(observation.get(field) == expected, f"观测字段 {field} 不符")
    for field in required_fields:
        require(field in observation and observation[field] not in (None, "", []), f"观测字段 {field} 缺失")
    if name == "route_block":
        require(0 < observation["external_refresh_ms"] <= 5000, "外部封路刷新超时")
    if name == "submit_timeout":
        require(3000 <= observation["elapsed_ms"] < 4500 and observation["snapshots"] >= 2,
                "超时边界或持续读取观测不符")

    target = observation["target"]
    requests = REQUEST_LINE.findall(log)
    indexed = {int(id_): key for key, id_, _, _ in requests}
    require(len(indexed) == len(requests), "实际请求编号重复")
    counts = {}
    for service, _, actual_target, include_active in requests:
        counts[service] = counts.get(service, 0) + 1
        if service != "list_scenario_workflows":
            require(actual_target == target, f"{service} 请求目标不符")
        if service in {"pause_workflow", "resume_workflow", "cancel_workflow"}:
            require(include_active == "true", f"{service} 运行中子任务参数不符")
    require(counts == REQUESTS[name], f"实际请求次数 {counts} 与登记值不符")
    replies = [record for record in records if record.get("step") == "console_reply"]
    require(replies and all(row.get("request_id") in indexed for row in replies) and
            len({row["request_id"] for row in replies}) == len(replies), "回执缺少实际请求对应项或重复")
    outcomes = [row.get("outcome") for row in replies]
    if name == "submit_timeout":
        require(outcomes == [2], "调度台没有记录超时")
    elif name == "route_block":
        require(outcomes == [0, 0, 0, 1], "缺少封路、解封或占用拒绝回执")
    elif name.endswith("_conflict"):
        require(outcomes.count(3) == (2 if name == "clear_scope_conflict" else 1), "缺少冲突回执")
    elif name == "workflow_control":
        require(all(indexed[row["request_id"]] != "cancel_workflow" or row["outcome"] == 0 for row in replies)
                and any(indexed[row["request_id"]] == "cancel_workflow" for row in replies), "缺少取消回执")
    else:
        require(all(outcome == 0 for outcome in outcomes), "回执未受理")
    return observation


def verify(stage, workspace, summary_path=None):
    scripts = workspace / "scripts"
    actual = stage / "console_ops_results.tsv"
    registry = scripts / "console_ops_cases.tsv"
    basis = workspace / "docs/baseline/m4_ops_results.tsv"
    mapping = scripts / "console_ops_case_map.tsv"
    report = stage / "ops_compare.md"
    expected = keyed(rows(registry))
    require(set(expected) == {f"ops-ui:{name}" for name in OBSERVATIONS}, "登记表未覆盖全部 10 项")
    results, issues = current_results(actual, {key: row["stack"] for key, row in expected.items()})
    if summary_path:
        compare = subprocess.run(
            [sys.executable, str(scripts / "acceptance_compare.py"), str(actual),
             str(basis) if basis.is_file() else "", str(registry), str(mapping), str(report)],
            capture_output=True, text=True)
        if compare.returncode and not issues:
            issues.append("专项结果或历史对照判定失败")
        if compare.stderr:
            issues.append(compare.stderr.strip())

    lines = []
    for key, registered in expected.items():
        name = registered["case"]
        try:
            status = results.get(key, ("缺失", ""))[0]
            require(status == "PASS", f"结果为 {status}")
            observation = check_case(stage, name, registered["stack"])
            lines.append(f"- `{key}` PASS，目标 `{observation['target']}`。原始证据 `ops-evidence/{name}/`。")
        except (OSError, ValueError, TypeError, KeyError, ElementTree.ParseError) as error:
            issues.append(f"{key}：{error}")
    if summary_path:
        with summary_path.open("a", encoding="utf-8") as summary:
            print(f"Ops 专项证据通过 {len(lines)}/{len(expected)}。", file=summary)
            for line in lines + [f"- {issue}" for issue in issues]:
                print(line, file=summary)
            if report.is_file():
                print(report.read_text(encoding="utf-8"), file=summary)
            if not basis.is_file():
                print("Ops 首轮无固定历史结果。历史对照不可比较，当次验收仍按登记用例判定。", file=summary)
    return {"expected": len(expected), "verified": len(lines)}, issues


def verify_terminal(stage, workspace):
    details, issues = verify(stage, workspace)
    exit_codes = rows(stage / "exit_codes.tsv")
    if exit_codes != [{"smoke_rc": "0", "ops_rc": "0", "evidence_rc": "0"}]:
        issues.append("阶段 7 三项原始退出码未全部为 0")
    metrics = rows(stage / "console_smoke.tsv")
    smoke = {row["metric"]: row["value"] for row in metrics}
    if (len(metrics) != len(smoke) or smoke.get("verdict") != "PASS" or smoke.get("console_rc") != "0"
            or not smoke.get("replay_check", "").endswith("；回放通过")):
        issues.append("无人操作冒烟或严格回放证据不完整")
    if (not 298 <= float(smoke["duration_s"]) <= 302 or smoke.get("network") != "workshop_2way"
            or smoke.get("qt_platform") != "offscreen" or int(smoke["robots_with_pose"]) != 6
            or not float(smoke["snapshot_hz"]) >= 0.8 or not float(smoke["traffic_hz"]) >= 0.8
            or not float(smoke["pose_hz_min"]) >= 5):
        issues.append("无人操作冒烟的时间、环境或频率不符合验收口径")
    services, publishers, violations = inspect(stage / "console.log")
    issues.extend(violations)
    details.update({"read_services": services, "business_publishers": publishers})
    return details, issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    parser.add_argument("stage", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--terminal", action="store_true")
    args = parser.parse_args()
    try:
        if args.terminal:
            details, issues = verify_terminal(args.stage, args.workspace)
        else:
            details, issues = verify(args.stage, args.workspace, args.summary)
    except (OSError, ValueError, KeyError, TypeError, csv.Error) as error:
        details, issues = {}, [str(error)]
    print(json.dumps({"details": details, "issues": issues}, ensure_ascii=False))
    return int(bool(issues))


if __name__ == "__main__":
    sys.exit(main())
