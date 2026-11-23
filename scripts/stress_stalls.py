#!/usr/bin/env python3
"""压测一组里的停摆与每车单数（acceptance-suite-rework specs：rolling 压测不得停摆）。

用法：scripts/stress_stalls.py <一组的输出目录> <rolling|whole_route>
读 state.csv（每秒一行车位）和 tasks.csv。第一行输出每车单数、单车最长静止和全队最长静止；
有问题时再逐行列出。判定只对 rolling：全队连续 60 秒不动，或任一车全程 0 单。whole_route 是对照组，
按规格不排队、也没有防饿死，只列不判。
退出码：0 = 通过，1 = rolling 停摆，2 = 文件读不到或没有数据，判断不了（两种模式都一样）。
"""
import csv
import math
import sys

FLEET_STALL_S = 60.0  # 正常组历史最长 13 秒，冻结组最短 129 秒（design D19）
MOVED_M = 0.05


def load(directory):
    with open(f"{directory}/state.csv", newline="") as file:
        rows = list(csv.DictReader(file))
    with open(f"{directory}/tasks.csv", newline="") as file:
        tasks = list(csv.DictReader(file))
    return rows, tasks


def measure(rows, tasks):
    """返回（每车 [车号, 单数, 最长静止秒]，全队最长静止秒，这段静止的起点）。"""
    end = float(rows[-1]["t_s"])
    fleet_moves = set()
    robots = []
    for number in range(1, int(rows[0]["robots"]) + 1):
        name = f"robot_{number}"
        moves = []
        previous = None
        for row in rows:
            point = (float(row[f"{name}_x"] or "nan"), float(row[f"{name}_y"] or "nan"))
            if previous is not None:
                if math.hypot(point[0] - previous[0], point[1] - previous[1]) > MOVED_M:
                    moves.append(float(row["t_s"]))
            previous = point
        fleet_moves.update(moves)
        times = [0.0] + moves + [end]
        still = max(b - a for a, b in zip(times, times[1:]))
        robots.append([name, sum(1 for task in tasks if task["robot"] == name), still])
    times = [0.0] + sorted(fleet_moves) + [end]
    fleet_still, since = max((b - a, a) for a, b in zip(times, times[1:]))
    return robots, fleet_still, since


def problems(mode, robots, fleet_still, since):
    if mode != "rolling":
        return []
    found = []
    if fleet_still >= FLEET_STALL_S:
        found.append(f"全队从第 {since:.0f} 秒起连续 {fleet_still:.0f} 秒不动")
    found += [f"{name} 全程 0 单" for name, count, _ in robots if count == 0]
    return found


def main():
    directory, mode = sys.argv[1:3]
    try:
        robots, fleet_still, since = measure(*load(directory))
    except (OSError, KeyError, ValueError, IndexError) as error:
        print(f"停摆检查无法进行：{type(error).__name__}: {error}")
        return 2
    each = "、".join(f"{name} {count} 单/静止 {still:.0f}s" for name, count, still in robots)
    print(f"{each}；全队最长静止 {fleet_still:.0f}s")
    found = problems(mode, robots, fleet_still, since)
    for item in found:
        print(item)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
