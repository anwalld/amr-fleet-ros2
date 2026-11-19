#!/usr/bin/env python3
"""验收用例 lint:assertion_alternation（acceptance-suite-rework design D7）。

rg 模式里的 "|" 是"或"：`rg "success=True|assigned_robot_id='robot_2'"` 只要输出里有 success=True
就通过，后面的业务期望根本没被检查。规则：或分支只允许用于同一事实的不同输出格式，
不同期望必须分开断言。

判定为"同一事实的不同格式"：把每个分支的 "=" 换成 ":"、去掉引号与空白、转小写后完全相同，
例如 success=True|success: true、allowed=False|allowed: false。
其余确有理由的或分支，在同一行末尾写 `# lint:allow-alternation <理由>` 放行。

检查范围：用例表里的每个 .sh 用例脚本和公共助手 scripts/robot_wait.sh 中的 rg 调用。
用法：scripts/lint_assertion_alternation.py [文件...]
退出码：0 = 没有违规，1 = 有违规（逐条打印 文件:行号）。
"""
import pathlib
import re
import sys

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
ALLOW = "lint:allow-alternation"
# rg 后跟可选参数，再跟一个带引号的模式。
RG = re.compile(r"""\brg\b(?:\s+-[-\w=]+)*\s+(?P<q>["'])(?P<pat>(?:\\.|(?!(?P=q)).)*)(?P=q)""")


def case_scripts():
    files = []
    for line in (SCRIPT_DIR / "acceptance_cases.tsv").read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.split("\t")
        if cols[0] != "suite" and len(cols) >= 3 and cols[2].endswith(".sh"):
            files.append(SCRIPT_DIR / cols[2])
    files.append(SCRIPT_DIR / "robot_wait.sh")
    out, seen = [], set()
    for f in files:
        if f.exists() and f not in seen:
            seen.add(f)
            out.append(f)
    return out


def split_alternatives(pattern):
    """按未转义、且不在 () [] 内的 | 切分。"""
    parts, buf, depth, i = [], "", 0, 0
    while i < len(pattern):
        ch = pattern[i]
        if ch == "\\" and i + 1 < len(pattern):
            buf += pattern[i : i + 2]
            i += 2
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth = max(0, depth - 1)
        if ch == "|" and depth == 0:
            parts.append(buf)
            buf = ""
        else:
            buf += ch
        i += 1
    parts.append(buf)
    return parts


def same_fact(parts):
    norm = {re.sub(r"[\s'\"\\]", "", p.replace("=", ":")).lower() for p in parts}
    return len(norm) == 1


def violations(path):
    found = []
    for no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if line.lstrip().startswith("#") or ALLOW in line:
            continue
        for m in RG.finditer(line):
            pat = m.group("pat")
            parts = split_alternatives(pat)
            if len(parts) > 1 and not same_fact(parts):
                found.append((no, pat))
    return found


def main(argv):
    files = [pathlib.Path(a) for a in argv] if argv else case_scripts()
    total, per_file = 0, {}
    for f in files:
        v = violations(f)
        if v:
            per_file[f.name] = len(v)
        for no, pat in v:
            total += 1
            print(f"{f.name}:{no}: 或分支合并了不同期望：rg \"{pat[:120]}\"")
    print(f"检查 {len(files)} 个文件，违规 {total} 处，涉及 {len(per_file)} 个文件")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
