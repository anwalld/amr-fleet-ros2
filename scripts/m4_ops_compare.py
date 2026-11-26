#!/usr/bin/env python3
"""根据已核验的固定 M4 Ops 参考导出逐项对照。"""

import argparse
import csv
import json
import math
from pathlib import Path
import sys

from m4_ops_preflight import keyed, rows

BASE_REPORT = "docs/baseline/nightly/2026-11-23-m4-core-rework6-r2.md"
WHOLE_REPORT = "docs/baseline/nightly/2026-11-23-m4-core-rework6-r2-supplement-6-whole-route.md"
STAGE7_REPORT = "docs/baseline/nightly/2026-11-23-m4-core-stage7-c3b8861.md"
MODES = ("whole_route", "rolling")
SCENARIOS = ("head_on", "ring3", "outside_wait")
SMOKE = {
    "snapshot_hz": 1.0, "traffic_hz": 1.0, "robots_with_pose": 6, "pose_hz_min": 7.71,
    "service_timeouts": 0, "latency_p95_ms": 32.71, "fps": 27.39, "paint_p95_ms": 2.163,
}


def write_tsv(path, header, report):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        writer.writerows(report)


def stage3(workspace, stage):
    basis = keyed(rows(workspace / "docs/baseline/acceptance_baseline.tsv"))
    now = keyed(rows(stage / "results.tsv"))
    mapping = {row["old_key"]: row for row in rows(workspace / "docs/baseline/acceptance_case_map.tsv")}
    report = []
    covered = set()
    for key, old in basis.items():
        mapped = mapping.get(key, {})
        target = mapped.get("new_key", key)
        current = now.get(target)
        covered.add(target)
        report.append((key, target, old["status"], current["status"] if current else "缺失",
                       old["seconds"], current["seconds"] if current else "", mapped.get("reason", "")))
    for key in sorted(set(now) - covered):
        report.append(("", key, "", now[key]["status"], "", now[key]["seconds"], "新增"))
    write_tsv(stage / "reference_compare.tsv",
              ("reference_key", "current_key", "reference_status", "current_status",
               "reference_seconds", "current_seconds", "mapping"), report)
    return "阶段 3 逐项对照见 reference_compare.tsv。", []


def report_rows(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("| "):
            yield [field.strip() for field in line.strip("|").split("|")]


def valid_window(group, count, mode):
    with (group / "state.csv").open(newline="") as stream:
        samples = list(csv.DictReader(stream))
    times = [float(sample["t_s"]) for sample in samples]
    if len(times) < 1795 or not all(math.isfinite(t) for t in times) or not (
            0 <= times[0] <= 5 and times[-1] >= 1795 and
            all(0 <= right - left <= 5 for left, right in zip(times, times[1:]))):
        return False
    return all(
        sample["mode"] == mode and sample["robots"] == str(count) and all(
            math.isfinite(float(sample[f"robot_{robot}_{axis}"]))
            for robot in range(1, count + 1) for axis in ("x", "y"))
        for sample in samples)


def stage5(workspace, stage):
    old = {(item[0], item[1]): tuple(map(float, item[4:7]))
           for item in report_rows(workspace / BASE_REPORT)
           if len(item) == 16 and item[0] in {"2", "4", "6"} and item[1] in MODES}
    # 后两项交叉核对自补测目录的原始 summary.json，均基于完整的 1800 秒样本。
    old[("6", "whole_route")] = (20.0, 40.0, 287.31)
    actual = {(row["robots"], row["mode"]): row for row in rows(stage / "metrics.tsv")}
    report = []
    issues = []
    for count in (2, 4, 6):
        for mode in MODES:
            key = (str(count), mode)
            source = WHOLE_REPORT if key == ("6", "whole_route") else BASE_REPORT
            group = stage / f"stress_{count}_{mode}"
            row = actual.get(key, {})
            compare = True
            try:
                meta = json.loads((group / "summary.json").read_text(encoding="utf-8"))
                capture = (group / "console.jsonl").is_file()
                comparable_config = (meta["network"] == "workshop_2way" and meta["mode"] == mode
                                     and meta["robots"] == count and abs(meta["duration_s"] - 1800) <= 5
                                     and capture == (key == ("6", "rolling")))
                if not comparable_config:
                    compare = False
                elif not valid_window(group, count, mode):
                    issues.append(f"{count} {mode} 采样窗口或车辆坐标不完整")
                    compare = False
                before = old[key]
                now = [float(row[field]) for field in ("tasks", "throughput_per_h", "mean_wait_route_s")]
                compare &= all(math.isfinite(value) for value in now)
            except (OSError, KeyError, ValueError, TypeError) as error:
                issues.append(f"{count} {mode} 压测证据不可读（{error}）")
                before, now, compare = old.get(key, ("", "", "")), ("", "", ""), False
            report.append((*key, source, "可比较" if compare else "不可比较", *before, *now,
                           *([round(right - left, 3) for left, right in zip(before, now)] if compare else ("", "", ""))))
    write_tsv(stage / "reference_compare.tsv",
              ("robots", "mode", "reference", "comparison", "reference_tasks", "reference_throughput",
               "reference_wait_s", "current_tasks", "current_throughput", "current_wait_s",
               "delta_tasks", "delta_throughput", "delta_wait_s"), report)
    return "阶段 5 六组指标、样本可比性及差值见 reference_compare.tsv。", issues


def stage6(workspace, stage):
    old = {(item[0], item[1]): item for item in report_rows(workspace / BASE_REPORT)
           if len(item) == 11 and item[0] in SCENARIOS}
    current = rows(stage / "deadlock_scenarios.tsv")
    actual = {(row["scenario"], row["round"]): row for row in current}
    issues = []
    if len(actual) != len(current) or set(actual) != set(old) or len(old) != 9:
        issues.append("死锁场景缺失、重复或轮数与参考不符")
    report = []
    for key, reference in old.items():
        row = actual.get(key, {})
        if row.get("verdict") != "PASS" or row.get("violations") != "0":
            issues.append(f"{' '.join(key)} 缺失、验收失败或存在安全违规")
        report.append((*key, reference[3], row.get("action", ""), reference[5], row.get("resolution_ms", ""),
                       reference[9], row.get("verdict", ""), row.get("violations", "")))
    write_tsv(stage / "reference_compare.tsv",
              ("scenario", "round", "reference_action", "current_action", "reference_resolution_ms",
               "current_resolution_ms", "reference_verdict", "current_verdict", "current_violations"), report)
    return "阶段 6 九项场景对照见 reference_compare.tsv。", issues


def stage7(workspace, stage):
    if not (workspace / STAGE7_REPORT).is_file():
        return "阶段 7 固定参考不可比较。", []
    actual = {row["metric"]: row["value"] for row in rows(stage / "console_smoke.tsv")}
    comparable = (actual.get("network") == "workshop_2way" and actual.get("qt_platform") == "offscreen"
                  and abs(float(actual.get("duration_s", 0)) - 300) <= 2 and actual.get("verdict") == "PASS")
    report = []
    for metric, before in SMOKE.items():
        now = actual.get(metric, "")
        report.append((metric, before, now, round(float(now) - before, 3) if comparable and now else ""))
    write_tsv(stage / "smoke_reference_compare.tsv", ("metric", "reference", "current", "difference"), report)
    return ("阶段 7 冒烟对照见 smoke_reference_compare.tsv。" if comparable else
            "阶段 7 冒烟配置、时长或判定与参考不符，指标差值不可比较。"), []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage_number", type=int, choices=(3, 5, 6, 7))
    parser.add_argument("workspace", type=Path)
    parser.add_argument("stage", type=Path)
    args = parser.parse_args()
    action = {3: stage3, 5: stage5, 6: stage6, 7: stage7}[args.stage_number]
    try:
        message, issues = action(args.workspace, args.stage)
        print(message)
        for issue in issues:
            print(issue)
        return int(bool(issues))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"固定参考不可比较或证据不可读（{error}）")
        return 1


if __name__ == "__main__":
    sys.exit(main())
