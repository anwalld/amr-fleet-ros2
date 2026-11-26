#!/usr/bin/env python3
"""核对 M4 Ops 夜跑装配的固定输入，并输出阶段 3 逐项参考。"""

import argparse
import csv
import hashlib
from pathlib import Path
import sys

REWORK_REV = "37f158c"
REFERENCES = {
    "docs/baseline/acceptance_baseline.tsv":
        "bb2716a4092e1052c67935834e017c9d3f5d65c8d06ebab6c2c3ce03cd30f6a3",
    "scripts/acceptance_cases.tsv":
        "78db36ae7a56b79a125493cd1f07a26d4f0f7357f84fa7e2db6d535cb9f34492",
    "docs/baseline/acceptance_case_map.tsv":
        "6863c8ee1ae5aa591856c4b87b8fcf3fc896d50f924e3a4addc9e31949f37ab5",
    "docs/baseline/nightly/2026-11-23-m4-core-rework6-r2.md":
        "66f08dd99cd3498ae91066cd8896fb5db3122056609b8cfe6b3940a2a9960e72",
    "docs/baseline/nightly/2026-11-23-m4-core-rework6-r2-supplement-6-whole-route.md":
        "c070a0cdca381c18b7814190d2b6addde78df60385ef972a75d1c05bef5f5139",
    "docs/baseline/nightly/2026-11-23-m4-core-stage7-c3b8861.md":
        "67af0f0376e41c307d70ed483a6f98d1cbe9472090774376a18c38fa77753926",
}


def rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader((line for line in stream if not line.startswith("#")),
                                   delimiter="\t"))


def keyed(rows_to_index):
    indexed = {}
    for row in rows_to_index:
        key = f"{row['suite']}:{row['case']}"
        if key in indexed:
            raise ValueError(f"重复用例 {key}")
        indexed[key] = row
    return indexed


def verify(root):
    for relative, expected in REFERENCES.items():
        actual = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"{relative} SHA-256 为 {actual}，预期 {expected}")
        print(f"{actual}  {relative}")
    reference = keyed(rows(root / "docs/baseline/acceptance_baseline.tsv"))
    if len(reference) != 110 or any(row["status"] != "PASS" for row in reference.values()):
        raise ValueError("阶段 3 参考必须是 110 个唯一的 PASS")
    registered = keyed(rows(root / "scripts/acceptance_cases.tsv"))
    mapping = {}
    for row in rows(root / "docs/baseline/acceptance_case_map.tsv"):
        old, new, reason = row["old_key"], row["new_key"], row["reason"]
        if old in mapping:
            raise ValueError(f"重复映射 {old}")
        if new == "-" and not reason.strip():
            raise ValueError(f"删除 {old} 未登记原因")
        mapping[old] = new
    covered = set()
    for old in reference:
        new = mapping.get(old, old)
        if new != "-":
            if new not in registered:
                raise ValueError(f"参考用例 {old} 无法对齐 {new}")
            covered.add(new)
    if set(registered) - covered:
        raise ValueError("用例表缺少参考：" + "、".join(sorted(set(registered) - covered)))
    print(f"阶段 3 固定参考 {REWORK_REV}，{len(covered)}/{len(registered)} 个登记用例可比较")
    return registered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("workspace", type=Path)
    args = parser.parse_args()
    try:
        verify(args.workspace)
        return 0
    except (OSError, ValueError, KeyError, csv.Error) as error:
        print(f"参考不可比较：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
