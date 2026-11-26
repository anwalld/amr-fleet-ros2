#!/usr/bin/env python3
"""核对无人操作冒烟期间的调度台请求与业务发布记录。"""

import argparse
from collections import Counter
import re
import sys
from pathlib import Path

READS = {"/v2/get_operator_snapshot", "/v2/list_stations", "/v2/list_station_routes"}


def inspect(path):
    log = path.read_text(encoding="utf-8")
    services = Counter(re.findall(r"console_service_request service=(\S+)", log))
    publishers = re.findall(r"console_business_publisher topic=(\S+)", log)
    missing = READS - services.keys()
    unexpected = services.keys() - READS
    issues = []
    if missing:
        issues.append("缺少读取服务：" + "、".join(sorted(missing)))
    if unexpected:
        issues.append("额外调用服务：" + "、".join(sorted(unexpected)))
    if publishers:
        issues.append("存在业务发布者：" + "、".join(publishers))
    if log.count("console_publisher_audit_complete") != 1:
        issues.append("没有完整的发布者核对记录")
    return services, publishers, issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("console_log", type=Path)
    args = parser.parse_args()
    try:
        services, publishers, issues = inspect(args.console_log)
    except OSError as error:
        print(f"无法核对调度台通信记录：{error}", file=sys.stderr)
        return 1
    for service, count in sorted(services.items()):
        print(f"{service}\t{count}")
    print(f"业务发布数\t{len(publishers)}")
    for issue in issues:
        print(issue)
    return int(bool(issues))


if __name__ == "__main__":
    sys.exit(main())
