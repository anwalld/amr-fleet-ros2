#!/usr/bin/env python3
"""验收用例 lint:script_timeouts（acceptance-suite-rework design D5）。

FastDDS 在高负载下会丢服务应答，不带超时的 ros2 调用会一直挂到用例的超时上限。
检查用例表里每个用例脚本以及公共助手 scripts/robot_wait.sh：下列调用必须在同一条命令里、
位于调用之前带上 timeout：
  ros2 service call / ros2 topic echo ... --once / ros2 action send_goal / tf2_echo
续行（行尾反斜杠）会先拼成一条逻辑行再检查；注释行忽略。
用法：scripts/lint_script_timeouts.py [文件...]   不带参数时检查用例表与公共助手。
退出码：0 = 没有违规，1 = 有违规（逐条打印 文件:行号）。
"""
import pathlib
import re
import sys

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
CALL = re.compile(
    r"ros2\s+service\s+call\b"
    r"|ros2\s+topic\s+echo\b[^|;&]*--once\b"
    r"|ros2\s+action\s+send_goal\b"
    r"|\btf2_echo\b"
)
# 同一管道段里、调用之前出现 timeout 即视为带超时。
SEGMENT_SPLIT = re.compile(r"\|\||&&|[|;&]|\$\(|`")


def case_scripts():
    table = SCRIPT_DIR / "acceptance_cases.tsv"
    files = []
    for line in table.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if cols[0] == "suite" or len(cols) < 3:
            continue
        files.append(SCRIPT_DIR / cols[2])
    files.append(SCRIPT_DIR / "robot_wait.sh")
    seen, out = set(), []
    for f in files:
        if f not in seen and f.suffix == ".sh" and f.exists():
            seen.add(f)
            out.append(f)
    return out


def logical_lines(path):
    """把续行拼起来，返回 (起始行号, 逻辑行)。"""
    buf, start = "", None
    for no, raw in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if start is None:
            start = no
        stripped = raw.strip()
        if not buf and stripped.startswith("#"):
            start = None
            continue
        if raw.rstrip().endswith("\\"):
            buf += raw.rstrip()[:-1] + " "
            continue
        buf += raw
        yield start, buf
        buf, start = "", None
    if buf:
        yield start, buf


def violations(path):
    found = []
    for no, line in logical_lines(path):
        for m in CALL.finditer(line):
            segment = SEGMENT_SPLIT.split(line[: m.start()])[-1]
            if not re.search(r"\b(timeout|gtimeout)\b", segment):
                found.append((no, line.strip()[:160]))
                break
    return found


def main(argv):
    files = [pathlib.Path(a) for a in argv] if argv else case_scripts()
    total = 0
    for f in files:
        for no, text in violations(f):
            total += 1
            print(f"{f.name}:{no}: 不带 timeout 的 ros2 调用：{text}")
    print(f"检查 {len(files)} 个文件，违规 {total} 处")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
