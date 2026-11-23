# 夜跑报告 2026-11-23

- 被测代码：`099c467-worktree-1a300d6f2be8-dagu-3867d3181f0d`
- 开始：2026-11-23 13:21:12；报告生成：2026-11-23 18:06:09
- 主机：lm-worker-1，4 核；负载 0.14 0.33 0.35
- 输出目录（CSV、日志留在虚拟机）：`/home/ubuntu/nightly/2026-11-23-m4-core-rework6-r2`

| 阶段 | 结论 | 用时 | 摘要 |
| --- | --- | --- | --- |
| 1 编译 | PASS | 62s | colcon build rc=0；Summary: 21 packages finished [1min 1s] |
| 2 单测 | PASS | 19s | colcon test-result rc=0；Summary: 667 tests, 0 errors, 0 failures, 0 skipped |
| 3 全量验收 | PASS | 4832s | 通过 110/110（判定：用例完整且全部 PASS 才算通过） |
| 4 一键演示 | PASS | 245s | 用时 235s；关键日志缺 0/4 条；动作失败 0 次 |
| 5 交通压测矩阵 | PASS | 10853s | 6 组中 0 组退出码非 0（1 = 有安全违规，2 = 车队没就绪），0 组没有完成任务，0 组有进程中途退出或无法检查，0 组停摆或无法检查；路网 workshop_2way，每组 30 分钟 |
| 6 死锁场景 | PASS | 638s | 调度台回放录像： |
| 7 调度台冒烟 | PASS | 396s | 冒烟脚本 rc=0 |

## 阶段 1 编译（PASS）

colcon build rc=0；Summary: 21 packages finished [1min 1s]

## 阶段 2 单测（PASS）

colcon test-result rc=0；Summary: 667 tests, 0 errors, 0 failures, 0 skipped

## 阶段 3 全量验收（PASS）

通过 110/110（判定：用例完整且全部 PASS 才算通过）
对比 /home/ubuntu/nightly/2026-11-22-rework4/stage3_acceptance/results.tsv：新失败 0，新通过 0，状态变化 0 项，新增 0 项，已删除 0 项（对比只作报告，不参与判定）

超预算（只提醒）：

| 用例 | 耗时 s | 预算 s |
| --- | --- | --- |
| `business:vda5050_state` | 21 | 20 |
| `ops:site_config_activation` | 11 | 10 |

## 阶段 4 一键演示（PASS）

用时 235s；关键日志缺 0/4 条；动作失败 0 次

## 阶段 5 交通压测矩阵（PASS）

6 组中 0 组退出码非 0（1 = 有安全违规，2 = 车队没就绪），0 组没有完成任务，0 组有进程中途退出或无法检查，0 组停摆或无法检查；路网 workshop_2way，每组 30 分钟

每车单数与最长静止（只对 rolling 判停摆）：
- 2台whole_route：robot_1 11 单/静止 89s、robot_2 8 单/静止 513s；全队最长静止 5s
- 2台rolling：robot_1 15 单/静止 60s、robot_2 18 单/静止 12s；全队最长静止 5s
- 4台whole_route：robot_1 3 单/静止 975s、robot_2 1 单/静止 806s、robot_3 7 单/静止 300s、robot_4 9 单/静止 229s；全队最长静止 5s
- 4台rolling：robot_1 16 单/静止 56s、robot_2 11 单/静止 203s、robot_3 10 单/静止 197s、robot_4 13 单/静止 49s；全队最长静止 4s
- 6台whole_route：robot_1 4 单/静止 994s、robot_2 5 单/静止 681s、robot_3 2 单/静止 1015s、robot_4 7 单/静止 405s、robot_5 1 单/静止 1577s、robot_6 2 单/静止 565s；全队最长静止 157s
- 6台rolling：robot_1 9 单/静止 46s、robot_2 9 单/静止 53s、robot_3 9 单/静止 47s、robot_4 10 单/静止 194s、robot_5 11 单/静止 38s、robot_6 8 单/静止 53s；全队最长静止 2s

调度台回放录像：
- stress_6_rolling/console.jsonl：87520 行，26M

| robots | mode | network | rc | tasks | throughput_per_h | mean_wait_route_s | violations | deadlocks | reroute | backoff | pause | resolve_ms_mean | resolve_ms_max | detect_us_mean | detect_us_max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2 | whole_route | workshop_2way | 0 | 19 | 38.0 | 74.45 | 0 | 3 | 0 | 3 | 0 | 532.9 | 965.6 | 31.7 | 142.4 |
| 2 | rolling | workshop_2way | 0 | 33 | 66.0 | 5.73 | 0 | 6 | 3 | 3 | 0 | 492.2 | 905.0 | 6.1 | 84.7 |
| 4 | whole_route | workshop_2way | 0 | 20 | 40.0 | 214.36 | 0 | 12 | 4 | 8 | 0 | 433.2 | 833.4 | 57.9 | 6133.9 |
| 4 | rolling | workshop_2way | 0 | 50 | 100.0 | 27.07 | 0 | 19 | 3 | 16 | 0 | 398.0 | 854.4 | 11.2 | 12948.8 |
| 6 | whole_route | workshop_2way | 0 | 21 | 42.0 | 196.99 | 0 | 17 | 3 | 14 | 0 | 253.9 | 996.0 | 74.7 | 521.9 |
| 6 | rolling | workshop_2way | 0 | 56 | 112.0 | 86.16 | 0 | 22 | 7 | 15 | 0 | 226.3 | 720.7 | 22.7 | 673.0 |

## 阶段 6 死锁场景（PASS）

调度台回放录像：
- scenario_head_on_1.console.jsonl：759 行，236K
- scenario_head_on_2.console.jsonl：778 行，240K
- scenario_head_on_3.console.jsonl：734 行，232K
- scenario_outside_wait_1.console.jsonl：1144 行，324K
- scenario_outside_wait_2.console.jsonl：1136 行，320K
- scenario_outside_wait_3.console.jsonl：1186 行，332K
- scenario_ring3_1.console.jsonl：2929 行，876K
- scenario_ring3_2.console.jsonl：3037 行，896K
- scenario_ring3_3.console.jsonl：1882 行，548K
场景脚本 rc=0；PASS 9/9 遍

| scenario | round | detected | action | yield_robot | resolution_ms | detect_s | tasks_done | violations | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| head_on | 1 | yes | reroute | robot_1 | 787 | 3.6 | 2/2 | 0 | PASS | - |
| head_on | 2 | yes | reroute | robot_1 | 274 | 3.8 | 2/2 | 0 | PASS | - |
| head_on | 3 | yes | reroute | robot_1 | 860 | 3.8 | 2/2 | 0 | PASS | - |
| ring3 | 1 | yes | backoff | robot_2 | 938 | 4.0 | 3/3 | 0 | PASS | - |
| ring3 | 2 | yes | backoff | robot_2 | 860 | 4.3 | 3/3 | 0 | PASS | - |
| ring3 | 3 | yes | backoff | robot_2 | 399 | 4.0 | 3/3 | 0 | PASS | - |
| outside_wait | 1 | yes | reroute | robot_1 | 300 | 3.6 | 3/3 | 0 | PASS | - |
| outside_wait | 2 | yes | reroute | robot_1 | 763 | 3.5 | 3/3 | 0 | PASS | - |
| outside_wait | 3 | yes | reroute | robot_1 | 63 | 4.0 | 3/3 | 0 | PASS | - |

## 阶段 7 调度台冒烟（PASS）

冒烟脚本 rc=0
```
metric	value
duration_s	300.0
snapshots	301
snapshot_hz	1.00
traffic_msgs	300
traffic_hz	1.00
events	8
event_hz	0.027
robots	6
robots_with_pose	6
pose_updates	13984
pose_hz_min	7.73
pose_hz.robot_1	7.77
pose_hz.robot_2	7.80
pose_hz.robot_3	7.76
pose_hz.robot_4	7.78
pose_hz.robot_5	7.73
pose_hz.robot_6	7.78
service_timeouts	0
latency_samples	13984
latency_p50_ms	18.26
latency_p95_ms	32.98
latency_max_ms	59.26
latency_skipped	0
frames	8206
fps	27.35
paint_p95_ms	2.234
console_rc	0
replay_check	OK event=8，graph=1，meta=1，pose=14173，snapshot=305，traffic=304；时长 305.1s；回放通过
verdict	PASS
```

## 提示：不带 timeout 的 ros2 service call

FastDDS 高负载下会丢服务应答，不带超时的调用会挂到 600 秒上限。
按行粗查（同一行没有 `timeout`）：0 个脚本共 0 处，其中共用助手 `scripts/robot_wait.sh` 0 处；明细 `/home/ubuntu/nightly/2026-11-23-m4-core-rework6-r2/untimed_service_calls.txt`。
