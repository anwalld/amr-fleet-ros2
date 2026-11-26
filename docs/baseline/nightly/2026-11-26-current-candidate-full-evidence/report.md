# 夜跑报告 2026-11-26

- 被测代码：`58b9000-merged-59a37fbf`
- 开始：2026-11-26 12:43:39；报告生成：2026-11-26 17:36:47
- 主机：lm-worker-1，4 核；负载 0.32 0.45 0.38
- 输出目录（CSV、日志留在虚拟机）：`/home/ubuntu/nightly/2026-11-26-merged-control-current-full`

| 阶段 | 结论 | 用时 | 摘要 |
| --- | --- | --- | --- |
| 1 编译 | PASS | 279s | colcon build rc=0；Summary: 21 packages finished [4min 38s] |
| 2 单测 | PASS | 21s | colcon test-result rc=0；Summary: 691 tests, 0 errors, 0 failures, 0 skipped |
| 3 全量验收 | PASS | 4805s | 阶段 3 使用固定参考 rework6-r2，摘要与覆盖见 reference.log。 |
| 4 一键演示 | PASS | 246s | 用时 235s；关键日志缺 0/4 条；动作失败 0 次 |
| 5 交通压测矩阵 | PASS | 10855s | 6 组中 0 组退出码非 0（1 = 有安全违规，2 = 车队没就绪），0 组没有完成任务，0 组有进程中途退出或无法检查，0 组停摆或无法检查；路网 workshop_2way，每组 30 分钟 |
| 6 死锁场景 | PASS | 693s | 调度台回放录像： |
| 7 调度台冒烟 | PASS | 637s | 冒烟脚本 rc=0，通信边界见 console_service_boundary.tsv |

## 阶段 1 编译（PASS）

colcon build rc=0；Summary: 21 packages finished [4min 38s]

## 阶段 2 单测（PASS）

colcon test-result rc=0；Summary: 691 tests, 0 errors, 0 failures, 0 skipped

## 阶段 3 全量验收（PASS）

阶段 3 使用固定参考 rework6-r2，摘要与覆盖见 reference.log。
通过 110/110（判定：用例完整且全部 PASS 才算通过）
对比 /home/ubuntu/robot-dagu-workspaces/merged-control-current-full.YlN4Os/robot_ws/docs/baseline/acceptance_baseline.tsv：新失败 0，新通过 0，状态变化 0 项，新增 0 项，已删除 0 项（对比只作报告，不参与判定）

超预算（只提醒）：

| 用例 | 耗时 s | 预算 s |
| --- | --- | --- |
| `ops:site_config_activation` | 14 | 10 |
阶段 3 逐项对照见 reference_compare.tsv。

## 阶段 4 一键演示（PASS）

用时 235s；关键日志缺 0/4 条；动作失败 0 次

## 阶段 5 交通压测矩阵（PASS）

6 组中 0 组退出码非 0（1 = 有安全违规，2 = 车队没就绪），0 组没有完成任务，0 组有进程中途退出或无法检查，0 组停摆或无法检查；路网 workshop_2way，每组 30 分钟

每车单数与最长静止（只对 rolling 判停摆）：
- 2台whole_route：robot_1 11 单/静止 92s、robot_2 8 单/静止 513s；全队最长静止 6s
- 2台rolling：robot_1 15 单/静止 107s、robot_2 18 单/静止 66s；全队最长静止 5s
- 4台whole_route：robot_1 5 单/静止 744s、robot_2 5 单/静止 655s、robot_3 7 单/静止 238s、robot_4 5 单/静止 880s；全队最长静止 5s
- 4台rolling：robot_1 12 单/静止 76s、robot_2 14 单/静止 61s、robot_3 13 单/静止 156s、robot_4 13 单/静止 89s；全队最长静止 4s
- 6台whole_route：robot_1 6 单/静止 522s、robot_2 0 单/静止 1800s、robot_3 7 单/静止 647s、robot_4 4 单/静止 480s、robot_5 3 单/静止 636s、robot_6 7 单/静止 304s；全队最长静止 7s
- 6台rolling：robot_1 11 单/静止 42s、robot_2 11 单/静止 77s、robot_3 9 单/静止 42s、robot_4 11 单/静止 40s、robot_5 10 单/静止 40s、robot_6 10 单/静止 46s；全队最长静止 2s

调度台回放录像：
- stress_6_rolling/console.jsonl：87536 行，26M

| robots | mode | network | rc | tasks | throughput_per_h | mean_wait_route_s | violations | deadlocks | reroute | backoff | pause | resolve_ms_mean | resolve_ms_max | detect_us_mean | detect_us_max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2 | whole_route | workshop_2way | 0 | 19 | 38.0 | 73.66 | 0 | 3 | 0 | 3 | 0 | 358.2 | 758.5 | 33.7 | 138.3 |
| 2 | rolling | workshop_2way | 0 | 33 | 66.0 | 5.37 | 0 | 2 | 2 | 0 | 0 | 172.9 | 273.0 | 6.9 | 66.8 |
| 4 | whole_route | workshop_2way | 0 | 22 | 44.0 | 182.03 | 0 | 13 | 2 | 11 | 0 | 314.0 | 928.3 | 59.9 | 203.6 |
| 4 | rolling | workshop_2way | 0 | 52 | 104.0 | 24.53 | 0 | 13 | 7 | 6 | 0 | 244.8 | 676.2 | 12.0 | 84.0 |
| 6 | whole_route | workshop_2way | 0 | 27 | 54.0 | 215.88 | 0 | 16 | 1 | 15 | 0 | 214.4 | 999.0 | 83.4 | 1738.4 |
| 6 | rolling | workshop_2way | 0 | 62 | 124.0 | 80.7 | 0 | 17 | 2 | 15 | 0 | 332.2 | 906.5 | 23.5 | 90.3 |
阶段 5 六组指标、样本可比性及差值见 reference_compare.tsv。

## 阶段 6 死锁场景（PASS）

调度台回放录像：
- scenario_head_on_1.console.jsonl：740 行，236K
- scenario_head_on_2.console.jsonl：729 行，232K
- scenario_head_on_3.console.jsonl：726 行，228K
- scenario_outside_wait_1.console.jsonl：1086 行，312K
- scenario_outside_wait_2.console.jsonl：1182 行，332K
- scenario_outside_wait_3.console.jsonl：1088 行，312K
- scenario_ring3_1.console.jsonl：3113 行，924K
- scenario_ring3_2.console.jsonl：3117 行，920K
- scenario_ring3_3.console.jsonl：3190 行，940K
场景脚本 rc=0；PASS 9/9 遍

| scenario | round | detected | action | yield_robot | resolution_ms | detect_s | tasks_done | violations | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| head_on | 1 | yes | reroute | robot_1 | 502 | 4.0 | 2/2 | 0 | PASS | - |
| head_on | 2 | yes | reroute | robot_1 | 523 | 3.9 | 2/2 | 0 | PASS | - |
| head_on | 3 | yes | reroute | robot_1 | 314 | 3.6 | 2/2 | 0 | PASS | - |
| ring3 | 1 | yes | backoff | robot_2 | 939 | 4.0 | 3/3 | 0 | PASS | - |
| ring3 | 2 | yes | backoff | robot_2 | 16 | 3.9 | 3/3 | 0 | PASS | - |
| ring3 | 3 | yes | backoff | robot_2 | 799 | 4.3 | 3/3 | 0 | PASS | - |
| outside_wait | 1 | yes | reroute | robot_1 | 59 | 3.6 | 3/3 | 0 | PASS | - |
| outside_wait | 2 | yes | reroute | robot_1 | 272 | 3.8 | 3/3 | 0 | PASS | - |
| outside_wait | 3 | yes | reroute | robot_1 | 33 | 3.6 | 3/3 | 0 | PASS | - |
阶段 6 九项场景对照见 reference_compare.tsv。

## 阶段 7 调度台冒烟（PASS）

冒烟脚本 rc=0，通信边界见 console_service_boundary.tsv
```
metric	value
duration_s	299.7
snapshots	300
snapshot_hz	1.00
traffic_msgs	300
traffic_hz	1.00
events	10
event_hz	0.033
robots	6
robots_with_pose	6
pose_updates	13883
pose_hz_min	7.68
pose_hz.robot_1	7.73
pose_hz.robot_2	7.68
pose_hz.robot_3	7.73
pose_hz.robot_4	7.68
pose_hz.robot_5	7.71
pose_hz.robot_6	7.79
service_timeouts	0
latency_samples	13883
latency_p50_ms	17.99
latency_p95_ms	32.07
latency_max_ms	35.91
latency_skipped	0
frames	7979
fps	26.62
paint_p95_ms	1.882
console_rc	0
network	workshop_2way
qt_platform	offscreen
service_boundary	console_service_boundary.tsv
replay_check	OK event=10，graph=1，meta=1，pose=13886，snapshot=300，traffic=300；时长 300.8s；回放通过
verdict	PASS
```
阶段 7 冒烟对照见 smoke_reference_compare.tsv。
Ops 场景运行器 rc=0，原始结果为 console_ops_results.tsv
Ops 专项证据通过 10/10。
- `ops-ui:workflow_submit` PASS，目标 `ops_workflow_544c70c7820e4542b7673a71001ae758`。原始证据 `ops-evidence/workflow_submit/`。
- `ops-ui:workflow_control` PASS，目标 `ops_workflow_375b73abf4fe4d29a8c2a467ae2c03f3`。原始证据 `ops-evidence/workflow_control/`。
- `ops-ui:fleet_submit_remote` PASS，目标 `ops_transport_37378a3a409d434da5f2b9d9d014453d`。原始证据 `ops-evidence/fleet_submit_remote/`。
- `ops-ui:fleet_submit_local` PASS，目标 `ops_transport_0b6420625d8c4d21a1fff00b2526ac6a`。原始证据 `ops-evidence/fleet_submit_local/`。
- `ops-ui:route_block` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/route_block/`。
- `ops-ui:fleet_enabled` PASS，目标 `robot_2`。原始证据 `ops-evidence/fleet_enabled/`。
- `ops-ui:clear_match` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/clear_match/`。
- `ops-ui:clear_owner_conflict` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/clear_owner_conflict/`。
- `ops-ui:clear_scope_conflict` PASS，目标 `route_edge:receiving__storage_a`。原始证据 `ops-evidence/clear_scope_conflict/`。
- `ops-ui:submit_timeout` PASS，目标 `ops_transport_62435f1b4e8a4cb7be84c0b91d12ff30`。原始证据 `ops-evidence/submit_timeout/`。
通过 10/10（判定：用例完整且全部 PASS 才算通过）
没有对比基准，本夜作为基线

Ops 首轮无固定历史结果。历史对照不可比较，当次验收仍按登记用例判定。
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

## 提示：不带 timeout 的 ros2 service call

FastDDS 高负载下会丢服务应答，不带超时的调用会挂到 600 秒上限。
按行粗查（同一行没有 `timeout`）：0 个脚本共 0 处，其中共用助手 `scripts/robot_wait.sh` 0 处；明细 `/home/ubuntu/nightly/2026-11-26-merged-control-current-full/untimed_service_calls.txt`。
