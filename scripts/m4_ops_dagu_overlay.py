#!/usr/bin/env python3
"""在隔离的 Ops 夜跑快照装配项目 Dagu 入口及阶段 7 原始证据复核。"""

import argparse
import hashlib
from pathlib import Path
import sys

FILES = ("dagu/robot-nightly.yaml", "scripts/dagu_nightly.py", "scripts/dagu_nightly_evidence.py")


def prepare(source, destination):
    if destination == source or (destination / ".git").exists():
        raise ValueError("目标必须是独立夜跑快照")
    if not (destination / "scripts/m4_ops_evidence.py").is_file():
        raise ValueError("隔离快照缺 Ops 证据判定脚本")
    replacements = {}
    for relative in FILES:
        path = source / relative
        contents = path.read_text(encoding="utf-8")
        print(f"输入 {hashlib.sha256(contents.encode()).hexdigest()}  {relative}")
        if relative.endswith("dagu_nightly_evidence.py"):
            hook = '        steps.append(("shared_acceptance", shared_acceptance, (output, workspace, rounds)))'
            if contents.count(hook) != 1 or "m4_ops_evidence" in contents:
                raise ValueError("当前 Dagu 共用验收步骤无法定位")
            contents = contents.replace("import math\n", "import math\n\nfrom m4_ops_evidence import verify_terminal\nfrom m4_alarm_evidence import verify as verify_alarm\n", 1)
            contents = contents.replace(hook, hook + '''
        steps.append(("m4_ops_stage7", verify_terminal, (output / "stage7_console_smoke", workspace)))
        steps.append(("m4_alarm_stage7", verify_alarm, (output / "stage7_console_smoke",)))''')
        if relative.endswith("dagu_nightly.py"):
            marker = '            budgets[stage] = {"seconds": seconds, "source": reason}\n    return budgets'
            if contents.count(marker) != 1:
                raise ValueError("当前 Dagu 耗时预算入口无法定位")
            contents = contents.replace(marker, marker.replace("    return budgets", '''    if 1 in budgets and not (ROOT / "build").is_dir():
        # 隔离快照无编译缓存，历史增量构建耗时不适用。
        budgets[1] = {"seconds": max(budgets[1]["seconds"], 600), "source": "全量冷构建预留"}
    if 7 in budgets:
        # 加入实际窗口 Ops 与 Alarm 的试跑和起栈开销。
        budgets[7]["seconds"] += 270
        budgets[7]["source"] = [budgets[7]["source"], "M4 Ops 与 Alarm 隔离试跑预算"]
    return budgets'''))
        replacements[relative] = contents
    for relative, contents in replacements.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents, encoding="utf-8")
        print(f"输出 {hashlib.sha256(contents.encode()).hexdigest()}  {relative}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("snapshot", type=Path)
    args = parser.parse_args()
    try:
        prepare(args.project.resolve(), args.snapshot.resolve())
        return 0
    except (OSError, ValueError) as error:
        print(f"Dagu 装配失败：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
