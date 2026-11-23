# 验收重整回查

2026-11-23。[基线](acceptance_baseline.tsv) 原样保存 [共同夜跑](nightly/2026-11-23-m4-core-rework6-r2.md) 阶段 3 的 110 项结果。共同夜跑的六车 whole_route 采样由 [同快照专项补测](nightly/2026-11-23-m4-core-rework6-r2-supplement-6-whole-route.md) 补齐。用例表继续沿用按 D5 用 [第 4 轮](nightly/2026-11-22-rework4.md) 实测值回填的预算和超时，这些预算已经共同夜跑验证。

## M1 路权拆分

| 原验收结论 | 第 4 轮回查 | 原证据局限 |
| --- | --- | --- |
| 三个交通验收通过，切分后调用可用 | `safety:route_locks`、`safety:intersection_lock`、`safety:traffic_deadlock` 全部 PASS。阶段 4 演示 PASS | M1 当时采用整条路权语义，当前用例覆盖演进后的语义。历史无回退的判断仍依据 M1 的 A/B 和迁移记录 |
| 533 项单测通过，演示里的 alpha 占用、beta 受阻与释放后通行可观察 | 阶段 2 的 667 项单测全部通过，阶段 4 的 4 条关键日志齐全，动作失败 0。路权状态由 `safety:route_locks` 再验证 | 原测试数是当时的快照。演示按当前滚动路权口径运行 |
| M0 73/99，M1 71/99。主要新增失败是已有不稳定项 | M1 的 99 个旧键按映射可追溯 96 个，其中原 PASS 的 69 个、原未通过的 27 个都映射到当前 PASS。另 3 个重复项删除。变化项 `mission:reprioritize`、`safety:localization_auto_recovery`、`business:scenario_facility_workflow`、`mission:docking_retry`、`business:scenario_workflow_shapes` 均 PASS | reprioritize 原来用瞬时状态轮询，可能漏过短任务。事件顺序断言修掉了这类漏看。新夜跑不能证明历史失败均出自同一个原因 |

## M2 分段申请和等待队列

| 原验收结论 | 第 4 轮回查 | 原证据局限 |
| --- | --- | --- |
| 6 台滚动轻量仿真连续 30 分钟，0 违规 | 阶段 5 双向路网的 6 台 rolling 完成 61 单，0 违规，每车都有完成单，全队最长静止 12 秒。2/4 台 rolling 也通过 | M2 实测用单向 `workshop`，本轮用 `workshop_2way`。历史的距离、吞吐及等待时间只适用于原测试环境 |
| E 类 5 个工作流越过旧的第二步入队拒绝 | `mission:station_order_batch`、`mission:workflow_pause_resume`、`business:scenario_workflows` 和合并后的 `business:scenario_workflow_shapes` 全部 PASS | 原 2 个行业脚本有 DDS 超时记录。新用例运行两种执行形态并检查 12 个模板受理，模板各步的路径由单测覆盖。其余模板的逐单实际完成没有单独验证 |
| 新路权语义、演示和单测通过 | `safety:route_locks`、`safety:intersection_lock`、`safety:traffic_deadlock`、`traffic:block_replan` 全部 PASS。阶段 2 和 4 PASS | 原交通验收曾有服务应答丢失。现在检查带超时，按增强后的路径见证判定 |
| 73/99 与 M1 逐项比对，接口检查后来修正 | M2 的 99 个旧键可追溯 96 个，其中原 PASS 71 个、原未通过 25 个全映射到当前 PASS。另 3 个重复项删除。`mission:queue_control`、`facility:showcase`、`mission:docking_retry`、`ops:legacy_usage`、`lint:interfaces_split` 均 PASS | 原计数和历史重跑结果保留。接口检查现在以注册与 CI 全工作区构建为依据 |

旧键的改名、合并与删除见 [用例映射](acceptance_case_map.tsv)。比较按旧键计数，一个合并后的新键可对应多个旧键。M1、M2 原始结果见 [M1 结果](m1_acceptance.tsv) 和 [M2 结果](m2_acceptance.tsv)。

## M3 死锁检测和解除

| 原验收结论 | 第 4 轮回查 | 原证据局限和更正 |
| --- | --- | --- |
| 等待图识别两车对向、三车成环和环外等待，并让任务继续 | 阶段 6 **9/9**，三类各 3/3、违规 0。针对旧失败的 `outside_wait` 3/3。`safety:traffic_deadlock` 通过 | 初版按 1 Hz 状态快照观察环，容易漏过已经解开的环。现在按让路指令里的环成员核对，再查任务完成与违规 |
| whole_route 重试等待可检出且能过期，找不到避让点时暂停并可解冻 | `traffic:whole_route_retry_wait`、`traffic:deadlock_pause_clear`、`traffic:block_replan` 均 PASS。差异化判据见夜跑阶段 5 | 防饿死的暂扣依赖等待队列，适用于 rolling。whole_route 最长全队静止 444 秒，作为对照数据记录 |
| 双向路网 6 组 30 分钟压测，解除提高可用吞吐 | 第 4 轮 6/6，无安全违规或中途退出，rolling 各组每车都完成任务。rolling 吞吐按 2/4/6 台为 54/108/122 单每小时 | 旧判据漏掉进程提前退出及 rolling 整队停摆。D17、D19 点出的受影响历史数字已在 [M3](M3.md) 标明 |
| 代理让路、回停车位，避免越界、永久无路或因幂等请求丢失挂住 | `fleet:agent_station_task` PASS。阶段 2 通过，包括 `agent_request_test`。阶段 5 无异常退出，阶段 6 的 `outside_wait` 全过 | 第 3 轮丢应答的具体客户端仍属推断。幂等重发行为以单测和当前场景结果为依据 |

## M1–M3 共同夜跑补测（2026-11-23）

已从 `lm-worker-1` 取回本轮验收结果、单测、演示、压测采样与死锁场景原始记录，按 [共同报告](nightly/2026-11-23-m4-core-rework6-r2.md) 的同一快照核对。阶段 3 实际有 110 个不同用例，全部 PASS。M1、M2 各自的 99 个旧键分别按映射追溯，96 个映射到本轮 PASS，另 3 个是已记录的重复删项。

| 里程碑 | 本轮补测结果 |
| --- | --- |
| M1 路权拆分 | `safety:route_locks`（47 秒）、`safety:intersection_lock`（12 秒）、`safety:traffic_deadlock`（6 秒）均通过。阶段 4 原始日志显示 alpha 获授予、beta 排队、alpha 释放后 beta 获授予，符合当前滚动语义。 |
| M2 分段申请和等待队列 | 排队链式用例 `mission:station_order_batch`、`mission:workflow_pause_resume`、`business:scenario_workflows` 和合并后的 `business:scenario_workflow_shapes` 均通过。`traffic:block_replan` 通过。双向路网六车 rolling 完整运行 30 分钟，1800 个状态样本，最大采样间隔 1.01 秒，完成 56 单，零安全违规，最小车距 2.945 米。历史 M2 的单向路网指标保留原口径。 |
| M3 等待图与死锁解除 | 阶段 6 的 `head_on`、`ring3`、`outside_wait` 各 3/3，全部检出和解除、各遍任务全部完成、零违规。`traffic:whole_route_retry_wait`、`traffic:deadlock_pause_clear` 在 `core` 栈通过，`fleet:agent_station_task` 通过。`traffic_deadlock_test` 29/29、`traffic_reservation_test` 47/47、`agent_request_test` 7/7。 |

阶段 5 的其余五组均有 1800 条状态样本。六车 `whole_route` 的原始采样只有 1644 条，从 1468.00 秒到 1625.11 秒间隔 157.11 秒，与宿主机合盖时段重合。该组原始数据保留为中断记录。19:11 同快照专项补测取得完整的 1800 条采样，最大间隔 1.03 秒，完成 20 单、零违规、无进程提前退出。该证据与共同夜跑其余五组并列核验，补齐 11.2。 [六车专项补测](nightly/2026-11-23-m4-core-rework6-r2-supplement-6-whole-route.md)

原始证据在报告所列虚拟机目录的 `stage3_acceptance/results.tsv`、`stage4_demo/demo.log`、`stage5_traffic_stress/metrics.tsv` 及各组 `state.csv` / `summary.json`、`stage6_deadlock_scenarios/deadlock_scenarios.tsv` 与 `stage2_unit_test/test_result.txt`。

## M4-core 9.1–9.4

2026-11-23 共同复验已核对，以下为更新结果。 [共同报告](nightly/2026-11-23-m4-core-rework6-r2.md)

| 任务 | 共同复验结果 |
| --- | --- |
| 9.1 | 两个调度台包单测通过，阶段 3 新失败 0，阶段 6 为 9/9。九份场景录像及六车 rolling 录像都核对到 graph、pose、traffic、snapshot，调度台冒烟与回放通过。 |
| 9.2 | M4 主检出已保存真实 `ring3.jsonl`，测试经过 Mac 和 Ubuntu 验证，固定回放及新增 QtTest 包含在共同待测快照。 |
| 9.3 | M4 主检出已更新基线。offscreen 延迟 p50 为 18.26 ms、p95 为 32.98 ms，帧率为 27.35 FPS；六车 rolling 吞吐 112 单每小时，位于历史区间，保留录像采集。 |
| 9.4 | M4 主检出严格校验通过，任务、交接及里程碑已更新。 |

验收重整 11.2 的六车 whole_route 采样缺口已由同快照 [专项补测](nightly/2026-11-23-m4-core-rework6-r2-supplement-6-whole-route.md) 补齐。

## OpenSpec 核验

按 `/opsx:verify` 检查提案、设计、5 份规格增量、任务清单及对应证据。共同夜跑基线与用例表的 110 个键和栈逐项一致，按第 4 轮证据核对 D5 预算与超时，严格校验通过。

| 范围 | 核验结果 |
| --- | --- |
| 完整性 | 验收补测和 11.2 已完成。10.3 的四项回查已写入本文，跨会话通知待发。 |
| 正确性 | 运行器隔离、跟踪、超时、旧键映射和接口覆盖已对照 `run_acceptance_baseline.sh`、`nightly.sh`、用例表与 lint。设施保持与释放对应 `mission_result_workflow_test` 和 `facility:actions`。车队重发对应 `agent_request_test` 和阶段 6。导航取消和暂停覆盖 `sequence_cancel_test`、`nav2_goal_step_test` 和三个导航验收用例。封路、过期重试与兜底暂停都有独立验收。 |
| 一致性 | 用例表列、映射表和脚本入口一致。预算规格已明确先倍乘再截取范围。D16 暂停语义留在独立变更，未计入本次修复。 |
| 验证边界 | 第 4 轮报告覆盖源码快照，预算回填和回查文档属于跑后产物，分别以结果表逐行核对、lint 和文档回查验证。M4 专用 ring3 回放测试没有作为本变更的通过证据。 |
