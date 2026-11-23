#!/usr/bin/env python3
"""调度台录像回放校验（acceptance-suite-rework specs：调度台冒烟附带回放校验）。

用法：scripts/console_replay_check.py <录像.jsonl> [robot_console_replay 可执行文件]
检查：
  1. 每行都是合法 JSON 且带 kind，第一行 kind 为 meta；
  2. graph、pose、traffic、snapshot 四类记录都至少有一条；
  3. 给了回放程序时，用它以 ×4 回放（--quit-after 按录像时长估算），退出码须为 0，
     且其标准错误里没有"跳过 N 行"。
输出一行 "OK ..." 或 "FAIL <原因>"；退出码 0 = 通过，1 = 不通过。
"""
import json
import pathlib
import subprocess
import sys

REQUIRED = ("graph", "pose", "traffic", "snapshot")


def main(argv):
    path = pathlib.Path(argv[0])
    replay = argv[1] if len(argv) > 1 else ""
    if not path.is_file() or path.stat().st_size == 0:
        print(f"FAIL 录像不存在或为空：{path}")
        return 1
    counts, bad, first_kind, last_t = {}, 0, None, 0.0
    for line in path.read_text(errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            rec = json.loads(line)
            kind = rec["kind"]
        except (ValueError, KeyError, TypeError):
            bad += 1
            continue
        first_kind = first_kind or kind
        counts[kind] = counts.get(kind, 0) + 1
        if isinstance(rec.get("t"), (int, float)):
            last_t = max(last_t, float(rec["t"]))
    problems = []
    if bad:
        problems.append(f"{bad} 行无法解析")
    if first_kind != "meta":
        problems.append(f"第一行 kind 为 {first_kind}，应为 meta")
    missing = [k for k in REQUIRED if not counts.get(k)]
    if missing:
        problems.append("缺少记录：" + "、".join(missing))
    if replay and not problems:
        quit_after = int(last_t / 4) + 10
        proc = subprocess.run(
            [replay, str(path), "--speed", "4", "--quit-after", str(quit_after)],
            capture_output=True, text=True, timeout=quit_after + 60,
            env={**__import__("os").environ, "QT_QPA_PLATFORM": "offscreen"})
        if proc.returncode != 0:
            problems.append(f"回放退出码 {proc.returncode}")
        if "跳过" in proc.stderr:
            problems.append("回放" + proc.stderr.strip().splitlines()[-1])
    summary = "，".join(f"{k}={counts[k]}" for k in sorted(counts))
    if problems:
        print("FAIL " + "；".join(problems) + f"（{summary}）")
        return 1
    print(f"OK {summary}；时长 {last_t:.1f}s" + ("；回放通过" if replay else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
