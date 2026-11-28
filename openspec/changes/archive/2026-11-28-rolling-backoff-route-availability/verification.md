## 设计核验

提案、设计、`route-reservation` 增量与任务均通过 OpenSpec 严格校验。rolling 的即时可用性筛选仅发生在原先首选为退避时。规则覆盖环外占用、暂扣和无可用备选，执行保持原逐段申请。现有取证与停摆判据继续作为共同夜跑验收依据。

## 实施核验

| 规格场景 | 实现与证据 |
| --- | --- |
| 环外车辆占用退避途中路口 | `PlanDeadlockResolution` 复用原排序和规划，`TrafficService` 仅对 rolling 启用。回归验证转选有可用路径的车。实地图局面中从 robot_4 经 jn_s 退到 park_2 改选 robot_3 经 packing 到 park_5。 |
| 全程可用的退避和改道均不存在 | GoogleTest 验证中间路口暂扣时先换车，全部可用备选被堵时返回原退避方案。whole_route 保持原方案。 |
| 夜跑与死锁实栈 | r2 阶段 5 六组通过。四车 rolling 完成 55 单、零安全违规，全队最长静止 4 秒。六组各 1800 条，最大采样间隔 1.02 秒。阶段 6 三场景各三轮均通过。 |

Ubuntu 规划器测试 31/31。原故障轨迹和时序复现保存在[联合夜跑证据](../../../docs/baseline/nightly/2026-11-24-m4-ops-alarm-evidence/)。

r2 单测汇总 683/683，含规划器 31/31。rolling 和 whole_route 接线与设计一致，阶段 5 与阶段 6 的验收证据齐全。共同作业因阶段 3 两个其他用例保留失败终态。 [r2 报告](../../../docs/baseline/nightly/2026-11-25-m4-ops-alarm-r2-evidence/report.md) · [独立证据复核](../../../docs/baseline/nightly/2026-11-25-m4-ops-alarm-r2-evidence/independent-review.json)
