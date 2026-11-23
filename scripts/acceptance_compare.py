#!/usr/bin/env python3
"""夜跑阶段 3 的判定与新旧对照（acceptance-suite-rework design D10）。

用法：scripts/acceptance_compare.py <本夜 results.tsv> <对比基准 results.tsv 或 ""> <用例表> <映射表> <输出 summary.md>
- 判定：本夜全部用例为 PASS 才算通过（退出码 0），否则退出码 1。不重跑、不豁免。
- 对照：先用映射表把基准结果换算到当前用例键——改名沿用旧状态；多个旧键映射到同一新键时全部 PASS
  才算 PASS；new_key 为 "-" 的列为"已删除"并附理由；基准里没有的键列为"新增"。对照只写进报告。
- 超预算：耗时超过用例表 budget_s（但没超时）的用例单独列出，只提醒。
基准为空字符串或文件不存在时，写明"没有对比基准，本夜作为基线"。
"""
import pathlib
import sys


def read_rows(path):
    out = []
    for line in pathlib.Path(path).read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        out.append(line.split("\t"))
    return out


def results(path):
    rows = read_rows(path)
    return {f"{r[0]}:{r[1]}": (r[3], r[4] if len(r) > 4 else "") for r in rows if r[0] != "suite"}


def current_results(path, expected):
    now, problems = {}, []
    rows = read_rows(path)
    if not rows or rows[0] != ["suite", "case", "kind", "status", "seconds"]:
        problems.append("结果表头错误")
    for row in rows[1:]:
        if len(row) != 5:
            problems.append("结果行列数错误：" + repr(row))
            continue
        suite, case, stack, status, seconds = row
        key = f"{suite}:{case}"
        if key in now:
            problems.append(f"重复用例：{key}")
            continue
        now[key] = (status, seconds)
        if key not in expected:
            problems.append(f"多出的用例：{key}")
        elif stack != expected[key]:
            problems.append(f"栈不符：{key}（{stack}，预期 {expected[key]}）")
        if not seconds.isascii() or not seconds.isdecimal():
            problems.append(f"耗时无效：{key}（{seconds}）")
    missing = sorted(expected.keys() - now.keys())
    if missing:
        problems.append("缺少用例：" + "、".join(missing))
    if not expected:
        problems.append("用例表没有用例")
    return now, problems


def main(argv):
    now_path, base_path, cases_path, map_path, out_path = argv[:5]
    case_rows = [r for r in read_rows(cases_path) if r[0] != "suite"]
    expected = {f"{r[0]}:{r[1]}": r[3] for r in case_rows}
    budget = {f"{r[0]}:{r[1]}": int(r[4]) for r in case_rows}
    now, problems = current_results(now_path, expected)
    if len(case_rows) != len(expected):
        problems.append("用例表有重复用例")
    mapping = [r for r in read_rows(map_path) if r[0] != "old_key"] if pathlib.Path(map_path).exists() else []

    lines = []
    passed = sum(1 for s, _ in now.values() if s == "PASS")
    failed = [k for k, (s, _) in now.items() if s != "PASS"]
    lines.append(f"通过 {passed}/{len(expected)}（判定：用例完整且全部 PASS 才算通过）")
    if problems:
        lines += ["", "结果完整性检查失败：", *[f"- {problem}" for problem in problems]]

    if base_path and pathlib.Path(base_path).exists():
        base = results(base_path)
        merged, deleted = {}, []
        for old, new, *reason in mapping:
            if old not in base:
                continue
            if new == "-":
                deleted.append((old, base[old][0], reason[0] if reason else ""))
            else:
                merged.setdefault(new, []).append(base[old][0])
        before = {k: s for k, (s, _) in base.items() if k not in {m[0] for m in mapping}}
        for new, states in merged.items():
            before[new] = "PASS" if all(s == "PASS" for s in states) else "/".join(sorted(set(states)))
        changes, added = [], []
        for key, (state, _) in now.items():
            if key not in before:
                added.append((key, state))
            elif before[key] != state:
                changes.append((key, before[key], state))
        new_fail = sum(1 for _, b, n in changes if b == "PASS" and n != "PASS")
        new_pass = sum(1 for _, b, n in changes if b != "PASS" and n == "PASS")
        lines.append(f"对比 {base_path}：新失败 {new_fail}，新通过 {new_pass}，状态变化 {len(changes)} 项，"
                     f"新增 {len(added)} 项，已删除 {len(deleted)} 项（对比只作报告，不参与判定）")
        if changes:
            lines += ["", "| 用例 | 之前 | 本夜 |", "| --- | --- | --- |"]
            lines += [f"| `{k}` | {b} | {n} |" for k, b, n in changes]
        if added:
            lines += ["", "新增用例：", "", "| 用例 | 本夜 |", "| --- | --- |"]
            lines += [f"| `{k}` | {s} |" for k, s in added]
        if deleted:
            lines += ["", "已删除用例：", "", "| 用例 | 上一次 | 理由 |", "| --- | --- | --- |"]
            lines += [f"| `{k}` | {s} | {r} |" for k, s, r in deleted]
    else:
        lines.append("没有对比基准，本夜作为基线")

    over = [(k, int(sec), budget[k]) for k, (s, sec) in now.items()
            if s == "PASS" and sec.isdigit() and k in budget and int(sec) > budget[k]]
    if over:
        lines += ["", "超预算（只提醒）：", "", "| 用例 | 耗时 s | 预算 s |", "| --- | --- | --- |"]
        lines += [f"| `{k}` | {sec} | {b} |" for k, sec, b in over]
    if failed:
        lines += ["", "未通过：" + "、".join(f"`{k}`（{now[k][0]}）" for k in failed)]

    pathlib.Path(out_path).write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 1 if failed or problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
