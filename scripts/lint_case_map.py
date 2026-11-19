#!/usr/bin/env python3
"""验收用例 lint:case_map（acceptance-suite-rework design D10）。

检查新旧映射表 docs/baseline/acceptance_case_map.tsv 与用例表 scripts/acceptance_cases.tsv 一致：
  1. 每行都有 old_key、new_key、reason 三列，reason 非空；
  2. new_key 不是 "-" 时必须存在于用例表；
  3. old_key 不得仍在用例表中（改名、合并、删除之后旧键就不该再出现）；
  4. 同一个 old_key 只出现一次。
用法：scripts/lint_case_map.py [映射表] [用例表]
退出码：0 = 一致，1 = 有问题（逐条打印）。
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def rows(path):
    for no, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        yield no, line.split("\t")


def main(argv):
    map_path = pathlib.Path(argv[0]) if argv else ROOT / "docs/baseline/acceptance_case_map.tsv"
    cases_path = pathlib.Path(argv[1]) if len(argv) > 1 else ROOT / "scripts/acceptance_cases.tsv"
    cases = {f"{c[0]}:{c[1]}" for _, c in rows(cases_path) if c[0] != "suite" and len(c) >= 2}
    problems, seen, n = [], set(), 0
    for no, cols in rows(map_path):
        if cols[0] == "old_key":
            continue
        n += 1
        if len(cols) < 3 or not cols[2].strip():
            problems.append(f"{map_path.name}:{no}: 需要 old_key、new_key、reason 三列且理由非空")
            continue
        old, new = cols[0], cols[1]
        if old in seen:
            problems.append(f"{map_path.name}:{no}: {old} 重复出现")
        seen.add(old)
        if new != "-" and new not in cases:
            problems.append(f"{map_path.name}:{no}: new_key {new} 不在用例表中")
        if old in cases:
            problems.append(f"{map_path.name}:{no}: old_key {old} 仍在用例表中")
    for p in problems:
        print(p)
    print(f"映射表 {n} 行，用例表 {len(cases)} 个用例，问题 {len(problems)} 处")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
