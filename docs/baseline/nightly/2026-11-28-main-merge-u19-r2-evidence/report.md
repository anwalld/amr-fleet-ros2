# 夜跑报告 2026-11-28

- 被测代码：`8766f99-main-merge-u19-r2`
- 开始：2026-11-28 13:28:59；报告生成：2026-11-28 18:25:15
- 主机：lm-worker-1，4 核；负载 0.22 0.28 0.31
- 输出目录（CSV、日志留在虚拟机）：`/home/ubuntu/nightly/2026-11-28-0355FtYr69MW3ThcBLkzXp`

| 阶段 | 结论 | 用时 | 摘要 |
| --- | --- | --- | --- |
| 1 编译 | PASS | 289s | colcon build rc=0；Summary: 21 packages finished [4min 49s] |
| 2 单测 | PASS | 21s | colcon test rc=0，test-result rc=0，total=724；Summary: 724 tests, 0 errors, 0 failures, 0 skipped |
| 3 全量验收 | PASS | 4904s | 阶段 3 使用固定参考 rework6-r2，摘要与覆盖见 reference.log。 |
| 4 一键演示 | PASS | 246s | 用时 235s；关键日志缺 0/4 条；动作失败 0 次 |
| 5 交通压测矩阵 | PASS | 10856s | 6 组中 0 组退出码非 0（1 = 有安全违规，2 = 车队没就绪），0 组没有完成任务，0 组有进程中途退出或无法检查，0 组停摆或无法检查；路网 workshop_2way，每组 30 分钟 |
| 6 死锁场景 | PASS | 694s | 调度台回放录像： |
| 7 调度台冒烟 | PASS | 713s | 冒烟脚本 rc=0，通信边界见 console_service_boundary.tsv |

## 阶段 1 编译（PASS）

colcon build rc=0；Summary: 21 packages finished [4min 49s]

## 阶段 2 单测（PASS）

colcon test rc=0，test-result rc=0，total=724；Summary: 724 tests, 0 errors, 0 failures, 0 skipped

## 阶段 3 全量验收（PASS）

阶段 3 使用固定参考 rework6-r2，摘要与覆盖见 reference.log。
通过 110/110（判定：用例完整且全部 PASS 才算通过）
对比 /home/ubuntu/robot-dagu-workspaces/8766f99-main-merge-u19-r2/workspace/docs/baseline/acceptance_baseline.tsv：新失败 0，新通过 0，状态变化 0 项，新增 0 项，已删除 0 项（对比只作报告，不参与判定）
阶段 3 逐项对照见 reference_compare.tsv。

## 阶段 4 一键演示（PASS）

用时 235s；关键日志缺 0/4 条；动作失败 0 次

## 阶段 5 交通压测矩阵（PASS）

6 组中 0 组退出码非 0（1 = 有安全违规，2 = 车队没就绪），0 组没有完成任务，0 组有进程中途退出或无法检查，0 组停摆或无法检查；路网 workshop_2way，每组 30 分钟

每车单数与最长静止（只对 rolling 判停摆）：
- 2台whole_route：robot_1 10 单/静止 91s、robot_2 11 单/静止 440s；全队最长静止 5s
- 2台rolling：robot_1 19 单/静止 75s、robot_2 22 单/静止 53s；全队最长静止 5s
- 4台whole_route：robot_1 5 单/静止 750s、robot_2 3 单/静止 634s、robot_3 7 单/静止 589s、robot_4 7 单/静止 717s；全队最长静止 6s
- 4台rolling：robot_1 17 单/静止 52s、robot_2 15 单/静止 195s、robot_3 15 单/静止 32s、robot_4 16 单/静止 69s；全队最长静止 2s
- 6台whole_route：robot_1 6 单/静止 257s、robot_2 2 单/静止 1303s、robot_3 3 单/静止 271s、robot_4 4 单/静止 273s、robot_5 1 单/静止 1606s、robot_6 5 单/静止 833s；全队最长静止 6s
- 6台rolling：robot_1 11 单/静止 38s、robot_2 11 单/静止 36s、robot_3 10 单/静止 36s、robot_4 12 单/静止 83s、robot_5 10 单/静止 82s、robot_6 8 单/静止 104s；全队最长静止 5s

调度台回放录像：
- stress_6_rolling/console.jsonl：87598 行，26M

| robots | mode | network | rc | tasks | throughput_per_h | mean_wait_route_s | violations | deadlocks | reroute | backoff | pause | resolve_ms_mean | resolve_ms_max | detect_us_mean | detect_us_max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2 | whole_route | workshop_2way | 0 | 21 | 42.0 | 73.67 | 0 | 6 | 0 | 6 | 0 | 400.7 | 787.7 | 36.4 | 409.5 |
| 2 | rolling | workshop_2way | 0 | 41 | 82.0 | 3.55 | 0 | 3 | 1 | 2 | 0 | 520.9 | 742.7 | 5.9 | 79.6 |
| 4 | whole_route | workshop_2way | 0 | 22 | 44.0 | 185.1 | 0 | 7 | 1 | 6 | 0 | 369.1 | 846.6 | 57.1 | 398.6 |
| 4 | rolling | workshop_2way | 0 | 63 | 126.0 | 21.27 | 0 | 16 | 9 | 7 | 0 | 571.6 | 945.6 | 13.9 | 3484.3 |
| 6 | whole_route | workshop_2way | 0 | 21 | 42.0 | 257.99 | 0 | 18 | 3 | 15 | 0 | 276.9 | 904.3 | 74.5 | 266.0 |
| 6 | rolling | workshop_2way | 0 | 62 | 124.0 | 80.92 | 0 | 12 | 6 | 6 | 0 | 402.6 | 983.8 | 23.9 | 110.8 |
阶段 5 六组指标、样本可比性及差值见 reference_compare.tsv。

## 阶段 6 死锁场景（PASS）

调度台回放录像：
- scenario_head_on_1.console.jsonl：747 行，236K
- scenario_head_on_2.console.jsonl：751 行，236K
- scenario_head_on_3.console.jsonl：728 行，232K
- scenario_outside_wait_1.console.jsonl：1109 行，316K
- scenario_outside_wait_2.console.jsonl：1112 行，320K
- scenario_outside_wait_3.console.jsonl：1072 行，312K
- scenario_ring3_1.console.jsonl：3199 行，940K
- scenario_ring3_2.console.jsonl：3161 行，932K
- scenario_ring3_3.console.jsonl：3135 行，924K
场景脚本 rc=0；PASS 9/9 遍

| scenario | round | detected | action | yield_robot | resolution_ms | detect_s | tasks_done | violations | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| head_on | 1 | yes | reroute | robot_1 | 228 | 3.7 | 2/2 | 0 | PASS | - |
| head_on | 2 | yes | reroute | robot_1 | 506 | 3.9 | 2/2 | 0 | PASS | - |
| head_on | 3 | yes | reroute | robot_1 | 585 | 3.8 | 2/2 | 0 | PASS | - |
| ring3 | 1 | yes | backoff | robot_2 | 210 | 3.9 | 3/3 | 0 | PASS | - |
| ring3 | 2 | yes | backoff | robot_2 | 793 | 4.1 | 3/3 | 0 | PASS | - |
| ring3 | 3 | yes | backoff | robot_2 | 461 | 4.2 | 3/3 | 0 | PASS | - |
| outside_wait | 1 | yes | reroute | robot_1 | 88 | 3.8 | 3/3 | 0 | PASS | - |
| outside_wait | 2 | yes | reroute | robot_1 | 421 | 3.5 | 3/3 | 0 | PASS | - |
| outside_wait | 3 | yes | reroute | robot_1 | 535 | 3.4 | 3/3 | 0 | PASS | - |
阶段 6 九项场景对照见 reference_compare.tsv。

## 阶段 7 调度台冒烟（PASS）

冒烟脚本 rc=0，通信边界见 console_service_boundary.tsv
```
metric	value
duration_s	300.5
snapshots	300
snapshot_hz	1.00
traffic_msgs	301
traffic_hz	1.00
events	10
event_hz	0.033
robots	6
robots_with_pose	6
pose_updates	13998
pose_hz_min	7.70
pose_hz.robot_1	7.80
pose_hz.robot_2	7.70
pose_hz.robot_3	7.71
pose_hz.robot_4	7.82
pose_hz.robot_5	7.80
pose_hz.robot_6	7.74
service_timeouts	0
latency_samples	13998
latency_p50_ms	18.61
latency_p95_ms	32.57
latency_max_ms	35.33
latency_skipped	0
frames	8300
fps	27.62
paint_p95_ms	1.960
console_rc	0
network	workshop_2way
qt_platform	offscreen
service_boundary	console_service_boundary.tsv
replay_check	OK event=10，graph=1，meta=1，pose=14194，snapshot=305，traffic=305；时长 305.6s；回放通过
verdict	PASS
```
阶段 7 冒烟对照见 smoke_reference_compare.tsv。
Ops 场景运行器 rc=0，原始结果为 console_ops_results.tsv
Ops 专项证据通过 10/10。
- `ops-ui:workflow_submit` PASS，目标 `ops_workflow_917cdb6c2b4d4194bbf3c10f7d5b330e`。原始证据 `ops-evidence/workflow_submit/`。
- `ops-ui:workflow_control` PASS，目标 `ops_workflow_f80b868000b24c29a3ea0372d8dc0984`。原始证据 `ops-evidence/workflow_control/`。
- `ops-ui:fleet_submit_remote` PASS，目标 `ops_transport_92b725bb074f47ba9cbf497852f321b9`。原始证据 `ops-evidence/fleet_submit_remote/`。
- `ops-ui:fleet_submit_local` PASS，目标 `ops_transport_026dc8d9efcc41edba2f18c6a8ad1fde`。原始证据 `ops-evidence/fleet_submit_local/`。
- `ops-ui:route_block` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/route_block/`。
- `ops-ui:fleet_enabled` PASS，目标 `robot_2`。原始证据 `ops-evidence/fleet_enabled/`。
- `ops-ui:clear_match` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/clear_match/`。
- `ops-ui:clear_owner_conflict` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/clear_owner_conflict/`。
- `ops-ui:clear_scope_conflict` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/clear_scope_conflict/`。
- `ops-ui:submit_timeout` PASS，目标 `ops_transport_907d6bfe9be84d6c90f6fec8e40c075a`。原始证据 `ops-evidence/submit_timeout/`。
通过 10/10（判定：用例完整且全部 PASS 才算通过）
对比 /home/ubuntu/robot-dagu-workspaces/8766f99-main-merge-u19-r2/workspace/docs/baseline/m4_ops_results.tsv：新失败 0，新通过 0，状态变化 0 项，新增 0 项，已删除 0 项（对比只作报告，不参与判定）

Ops 专项证据 rc=0，详情见 ops_evidence.json
Alarm 运行器 rc=0，证据 rc=0，详情见 alarm_evidence.json 和 alarm-evidence/
case	result
alarm_events_and_recovery	PASS
alarm_sim_stop	PASS
alarm_installation	PASS
任务控制重启检查 rc=0，详情见 task-control-restart/results.tsv
case	status
d16	PASS
fleet	PASS
dispatcher-state	PASS
d16-state	PASS
任务控制仿真 rc=0，详情见 task-control-scenarios/results.tsv
case	status
d16-delayed-reply	PASS
d16-route-hazard	PASS
d16-motion-cutover	PASS
fleet-rolling	PASS
fleet-whole-route	PASS
fleet-cancel-paused	PASS
fleet-restart-held	PASS
fleet-restart-missing-route	PASS
fleet-restart-paused-rolling	PASS
fleet-restart-paused-whole-route	PASS

## 提示：不带 timeout 的 ros2 service call

FastDDS 高负载下会丢服务应答，不带超时的调用会挂到 600 秒上限。
按行粗查（同一行没有 `timeout`）：0 个脚本共 0 处，其中共用助手 `scripts/robot_wait.sh` 0 处；明细 `/home/ubuntu/nightly/2026-11-28-0355FtYr69MW3ThcBLkzXp/untimed_service_calls.txt`。
