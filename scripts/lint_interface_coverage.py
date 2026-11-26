#!/usr/bin/env python3
"""验收用例 lint:interface_coverage（acceptance-suite-rework design D8）。

从 src/*/src/*.cpp 提取以字面名称创建的服务、发布的话题、动作服务器，再加 scripts/rest_api_gateway.py
的路由，和接口覆盖表 docs/baseline/interface_coverage.tsv（列 kind name owner cases note）对照：
  1. 每个提取出的接口在表里都有一行（kind + name）；
  2. 表里 cases 列的每个用例键都在用例表 scripts/acceptance_cases.tsv 中；
  3. 表里的接口确实被它列出的某个用例脚本（或公共助手 robot_wait.sh）引用；
     note 为 via:/v2/submit_order 的旧入口除外（它们经统一入口转发，另有兼容用例）。
表里多出的、源码中已不存在的接口也报出来（接口改名或删除后要同步）。

用法：
  scripts/lint_interface_coverage.py              检查，退出码 0 = 一致，1 = 有问题
  scripts/lint_interface_coverage.py --draft      按"哪些用例脚本引用了它"生成初稿到标准输出
"""
import pathlib
import re
import sys

from m4_ops_evidence import REQUESTS as OPS_REQUESTS
from m4_ops_preflight import rows

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "scripts"
TABLE = ROOT / "docs/baseline/interface_coverage.tsv"
CASES = SCRIPTS / "acceptance_cases.tsv"
OPS_CASES = SCRIPTS / "console_ops_cases.tsv"
HELPERS = [SCRIPTS / "robot_wait.sh", SCRIPTS / "_traffic_self_common.sh"]

PATTERNS = {
    "service": re.compile(r'create_service<[^>]+>\s*\(\s*"([^"]+)"'),
    "topic": re.compile(r'create_publisher<[^>]+>\s*\(\s*"([^"]+)"'),
    "action": re.compile(r'rclcpp_action::create_server<[^>]+>\s*\([^"]{0,200}"([^"]+)"', re.S),
}
REST_ROUTE = re.compile(r"""['"](/(?:api|v[0-9])/[a-z_/{}<>:-]+)['"]""")


def extract():
    found = {}
    for path in sorted(ROOT.glob("src/*/src/*.cpp")):
        text = path.read_text(errors="ignore")
        owner = f"{path.parts[-3]}/{path.stem}"
        for kind, pattern in PATTERNS.items():
            for name in pattern.findall(text):
                if name.startswith("/"):
                    found.setdefault((kind, name), set()).add(owner)
    gateway = SCRIPTS / "rest_api_gateway.py"
    if gateway.exists():
        for name in REST_ROUTE.findall(gateway.read_text(errors="ignore")):
            found.setdefault(("rest", name), set()).add("scripts/rest_api_gateway")
    return found


def case_table():
    cases = {}
    for line in CASES.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if cols[0] != "suite" and len(cols) >= 3:
            cases[f"{cols[0]}:{cols[1]}"] = SCRIPTS / cols[2]
    for row in rows(OPS_CASES):
        cases[f"{row['suite']}:{row['case']}"] = SCRIPTS / "run_console_ops.sh"
    cases["alarm-ui:alarm_sim_stop"] = SCRIPTS / "run_console_alarm.sh"
    return cases


def references(name, path):
    if not path.exists():
        return False
    return re.search(re.escape(name) + r"(?![a-z_0-9])", path.read_text(errors="ignore")) is not None


def covers(name, case, path):
    if case.startswith("ops-ui:"):
        service = name.removeprefix("/v2/")
        return (name.startswith("/v2/") and
                service in OPS_REQUESTS.get(case.split(":", 1)[1], {}))
    return references(name, path)


def draft():
    cases = case_table()
    print("# 接口覆盖表（acceptance-suite-rework design D8）。lint:interface_coverage 检查它与 src/ 一致。")
    print("# cases：覆盖该接口的用例键（逗号分隔）；note：断言要点，或 via:/v2/submit_order（旧入口经统一入口转发）。")
    print("kind\tname\towner\tcases\tnote")
    for (kind, name), owners in sorted(extract().items()):
        hits = [k for k, p in sorted(cases.items()) if covers(name, k, p)]
        if not hits and any(references(name, h) for h in HELPERS):
            hits = ["(helper)"]
        print(f"{kind}\t{name}\t{','.join(sorted(owners))}\t{','.join(hits)}\t")


def check():
    cases = case_table()
    found = extract()
    table, problems = {}, []
    for no, line in enumerate(TABLE.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t") + ["", "", "", ""]
        if cols[0] == "kind":
            continue
        table[(cols[0], cols[1])] = (no, [c for c in cols[3].split(",") if c], cols[4])
    for key in sorted(found):
        if key not in table:
            problems.append(f"未登记的接口：{key[0]} {key[1]}（{','.join(sorted(found[key]))}）")
    for key, (no, keys, note) in sorted(table.items()):
        if key not in found:
            problems.append(f"{TABLE.name}:{no}: 源码中已没有 {key[0]} {key[1]}")
            continue
        if not keys:
            problems.append(f"{TABLE.name}:{no}: {key[1]} 没有覆盖用例")
            continue
        missing = [k for k in keys if k not in cases]
        if missing:
            problems.append(f"{TABLE.name}:{no}: {key[1]} 列出的用例不在用例表中：{','.join(missing)}")
            continue
        if note.startswith("via:"):
            continue
        if not any(covers(key[1], k, cases[k]) for k in keys):
            problems.append(f"{TABLE.name}:{no}: {key[1]} 列出的用例脚本都没有引用它：{','.join(keys)}")
    for p in problems:
        print(p)
    print(f"源码接口 {len(found)} 个，覆盖表 {len(table)} 行，问题 {len(problems)} 处")
    return 1 if problems else 0


if __name__ == "__main__":
    if "--draft" in sys.argv[1:]:
        draft()
        sys.exit(0)
    sys.exit(check())
