#!/usr/bin/env python3
"""列出压测中途退出的进程（acceptance-suite-rework specs：压测期间进程不得中途退出）。

用法：scripts/stress_process_exits.py <launch.log>
关栈之前的 "process has died" 和 "process has finished cleanly" 都算中途退出。关栈从压测脚本写的
标记开始；没有标记的旧日志退而用第一条 signal_handler 行，两者都没有时整份日志都算关栈之前。
退出码：0 = 没有，1 = 有（逐行打印进程名与方式），2 = 日志读不到或为空，判断不了。
"""
import re
import sys

STOP_MARKER = "[stress] stopping stack"
SIGNAL_LINE = "signal_handler(SIGINT/SIGTERM)"
EXIT_LINE = re.compile(
    r"\[([^\[\]]+)\]: process has (died \[.*?exit code (-?\d+)|finished cleanly)")


def early_exits(lines):
    stop = len(lines)
    for mark in (STOP_MARKER, SIGNAL_LINE):
        found = next((i for i, line in enumerate(lines) if mark in line), None)
        if found is not None:
            stop = found
            break
    exits = []
    for line in lines[:stop]:
        match = EXIT_LINE.search(line)
        if match:
            name, _, code = match.groups()
            exits.append(f"{name} exit={code or 0}")
    return exits


def main():
    try:
        with open(sys.argv[1], errors="replace") as log:
            lines = log.read().split("\n")
    except OSError as error:
        print(f"launch.log unreadable: {error.strerror}")
        return 2
    if not any(lines):
        print("launch.log empty")
        return 2
    exits = early_exits(lines)
    for item in exits:
        print(item)
    return 1 if exits else 0


if __name__ == "__main__":
    sys.exit(main())
