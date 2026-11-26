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
