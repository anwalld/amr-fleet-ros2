#!/usr/bin/env python3
"""从调度台回放文件（JSONL）截取一段，t 平移到从 0 开始（M4）。

  python3 scripts/console_replay_trim.py <输入> <输出> --from T0 --to T1
  python3 scripts/console_replay_trim.py <输入> <输出> --around-deadlock [--before 10 --after 20]

- 保留 meta，追加 trimmed_from、trimmed_to（原文件中的秒数）。
- T0 之前最近的一份 graph 记为 t=0，保证截出的片段能单独画出路网。
- 窗口 [T0, T1] 内的记录原样保留，t 减去 T0。
- --around-deadlock：窗口取第一次出现非空 deadlock_robot_ids 前 BEFORE 秒，
  到其后 resolved_count 第一次增加后 AFTER 秒。
只用标准库，Mac 上也能直接运行。
"""

import argparse
import json
import sys


def load(path):
    records = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            if isinstance(rec, dict) and "t" in rec and "kind" in rec:
                records.append(rec)
    if not records or records[0].get("kind") != "meta":
        raise SystemExit(f"{path}: 第一行不是 meta")
    return records


def deadlock_window(records, before, after):
    start = None
    base_resolved = None
    for rec in records:
        if rec["kind"] != "traffic":
            continue
        data = rec.get("data", {})
        if start is None:
            if data.get("deadlock_robot_ids"):
                start = rec["t"]
                base_resolved = data.get("resolved_count", 0)
            continue
        if data.get("resolved_count", 0) > base_resolved:
            return max(0.0, start - before), rec["t"] + after
    if start is None:
        raise SystemExit("文件中没有检出过死锁（deadlock_robot_ids 一直为空）")
    raise SystemExit("检出死锁后没有找到 resolved_count 增加")


def trim(records, t0, t1):
    meta = dict(records[0])
    meta["trimmed_from"] = round(t0, 4)
    meta["trimmed_to"] = round(t1, 4)
    meta["t"] = 0.0
    out = [meta]
    carried = None
    for rec in records[1:]:
        if rec["kind"] == "graph" and rec["t"] <= t0:
            carried = rec
    if carried is not None:
        g = dict(carried)
        g["t"] = 0.0
        out.append(g)
    for rec in records[1:]:
        if t0 <= rec["t"] <= t1 and rec is not carried:
            r = dict(rec)
            r["t"] = round(rec["t"] - t0, 4)
            out.append(r)
    return out


def main():
    p = argparse.ArgumentParser(description="截取调度台回放片段")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--from", dest="t0", type=float)
    p.add_argument("--to", dest="t1", type=float)
    p.add_argument("--around-deadlock", action="store_true")
    p.add_argument("--before", type=float, default=10.0)
    p.add_argument("--after", type=float, default=20.0)
    args = p.parse_args()

    records = load(args.input)
    if args.around_deadlock:
        t0, t1 = deadlock_window(records, args.before, args.after)
    else:
        if args.t0 is None or args.t1 is None:
            p.error("需要 --from 和 --to，或 --around-deadlock")
        t0, t1 = args.t0, args.t1
    if t1 <= t0:
        p.error("--to 必须大于 --from")
    out = trim(records, t0, t1)
    with open(args.output, "w", encoding="utf-8") as f:
        for rec in out:
            f.write(json.dumps(rec, ensure_ascii=False, separators=(",", ":")) + "\n")
    print(f"{args.input} [{t0:.2f}, {t1:.2f}] -> {args.output}：{len(out)} 行", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
