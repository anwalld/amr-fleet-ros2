# 核验

## 设计核验

提案、设计、规格及任务均以滚动路权退避候选为范围，使用仿真实际车位归属。已核对与旧有可通行路径优先、逐段授予和无备选回退的关系。`openspec validate rolling-backoff-home-preference --strict` 通过。

## 实施核验

2026-11-25，北京时间。合并候选 `codex/merged-nightly-candidate` 基于 `58b9000`，使用未提交修复。Ubuntu Jazzy 上的 `robot_tasks` 和 `robot_bringup` 编译通过。任务中枢的车位归属由 fleet_sim 传入，默认空配置沿用既有规划，重复车位按共享处理。

规划单测 32/32。新增场景断言公共退避点优先于他车空车位、没有合适备选时回退至原方案、whole_route 的原选择以及共享车位的选择。构建和单测日志在 [专项证据](../../../docs/baseline/nightly/2026-11-25-merged-control-hardening-evidence/)。

四车 rolling 的实际 `/mission_runner_node` 返回 `park_1,park_2,park_3,park_4`，与四车停车位一致，原始值见专项证据中的 `rolling-home-param.log`。

四车 rolling 专项跑满 30 分钟，1800 条采样、最大间隔 1.01 秒、54 单、零违规、无节点提前退出、最长全队静止 6 秒，20 次死锁全部解除。`rolling-review.json` 根据原始采样、订单表、违规表和启动日志独立计算。专项增加只读回放采集用于诊断。正式共同夜跑已启动，任务 1.3 等正式阶段 5 及后验通过后结束。

## 2026-11-26 正式续跑证据

同快照正式续跑第 5 阶段及采样后验通过。六组 30 分钟均有 1800 条采样。四车 rolling 完成 58 单、最大采样间隙 1.01 秒、零违规、无进程提前退出、最长全队静止 4 秒。任务 1.3 的压测判据已满足。 [原始证据独立复核](../../../docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/independent-review.json)

## 11 月 26 日完整共同夜跑核验

修订 `58b9000-merged-59a37fbf`。同轮四车 rolling 52 单、零违规、最大采样间隔 1.03 秒、最长全队静止 4 秒，无进程提前退出。退避专项已闭环，从后续专项队列移出。 [压测原始报告](../../../docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/stage5_traffic_stress/summary.md) [完整后验](../../../docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/postcheck.json)
