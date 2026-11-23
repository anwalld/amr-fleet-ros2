#!/usr/bin/env python3
"""验收用例 lint:service_response（acceptance-suite-rework design D7）。

ros2 service call 会回显请求，断言会被请求参数骗过。用例脚本与公共助手一律经
robot_wait.sh 的 service_response 调用服务，不得直接写 ros2 service call。
用法：scripts/lint_service_response.py [文件...]
退出码：0 = 没有违规，1 = 有违规（逐条打印 文件:行号）。
"""
import pathlib
import re
import sys

from lint_script_timeouts import SCRIPT_DIR, case_scripts, logical_lines

CALL = re.compile(r"ros2\s+service\s+call\b")
WRAPPER = 'out="$(timeout --kill-after=5 "${limit}" ros2 service call "$@")"'


def main(argv):
    if argv:
        files = [pathlib.Path(a) for a in argv]
    else:
        files = case_scripts() + sorted(SCRIPT_DIR.glob("_*common.sh"))
    total = 0
    for path in files:
        for no, line in logical_lines(path):
            if not CALL.search(line) or (path.name == "robot_wait.sh" and WRAPPER in line):
                continue
            total += 1
            print(f"{path.name}:{no}: 请改用 service_response：{line.strip()[:140]}")
    print(f"检查 {len(files)} 个文件，违规 {total} 处")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
