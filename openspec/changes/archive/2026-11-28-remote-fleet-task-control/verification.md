# 设计核验记录

按仓库 /opsx:verify 核对本变更。OpenSpec 的 status 和 instructions apply 均可解析 proposal、design、specs 和 tasks。严格校验结果见[共同核验记录](../../../../../robot/openspec/changes/explore-task-control-boundaries/verification.md)。

| 维度 | 结果 |
|---|---|
| 完整性 | 5 条新增或修改需求，11 个规格场景，实施清单 0/5。任务 1 至 4 分别承接新增需求，任务 5 承接修订的主规格及原场景回归。 |
| 正确性 | 已比对当前代码与目标行为。现有代码只提供部分基础，尚无这套变更的完整实现或专项运行证据。 |
| 一致性 | 已检查目标、执行端、状态转换、观测条件及验收边界；实现对设计的遵循、代码风格与运行阈值有效性待实施核验。 |

核对依据：fleet_robot_agent_node.cpp 的默认空闲回位、任务轮询及进度回报；UpdateRemoteTaskState.srv 只有任务状态上报字段；Ops/Alarm 中存在工作流写操作与车队停止门控。

归档前阻塞项：完成 tasks.md 的五项任务并提交覆盖上述场景的原始证据。实际集成须复核 Ops/Alarm 当时版本，验证显式移车对两种路权模式均能安全交接。 当前结论只覆盖设计阶段。

## 本地实施核验 2026-11-25

工作区为 `/Users/mima0000/CLionProjects/robot-worktrees/remote-fleet-task-control`，分支 `codex/remote-fleet-task-control`，基线 `58b9000`。Ubuntu Jazzy 隔离目录为 `/tmp/robot-remote-local.1Mj6o8`。

- 调度侧 `/v2/control_remote_fleet_task` 按现有任务分配记录转发给对应代理。代理按任务 ID、执行 ID 和修订号核对请求，重试可按原凭据取回已知结果。超时返回结果未知，可按原凭据查询。
- 独立仿真验证使用代理参数 `remote_task_control_enabled:=true` 与模拟车队急停心跳。该参数当前默认关闭，Ops/Alarm 联调时须接入实际急停发布端。
- 新接口生成成功，当前工作区的车队代理和任务中枢已编译并链接。停稳观测 2/2，车队状态 18/18，既有空闲回位单测 3/3。
- `rolling-remote-control.log`、`whole_route-remote-control.log` 和 `rolling-cancel-paused.log` 验证停稳回执、急停时拒绝恢复、同修订重试、途中和暂停后取消、保留当前路段、拒绝冲突申请，以及显式移车到达路段出口后的释放。任务 2.1 完成。

代理进程重启后的驻留和请求结果对账仍待实施。Ops/Alarm 联调后需核对调度台反馈、默认回位与任务接续、实跑采样证据以及运维清占用与任务驻留的交互。当前日志来自轻量仿真和模拟急停发布者。

`warm-task-cancel-paused.log` 先在起点完成一单，保留默认空闲回位配置，再在通道上暂停并取消下一单。独立里程计观测车辆留在当前路段，直到显式移车后才到达出口。结合既有回位单测，任务 5.1 完成。另一次 rolling 检查覆盖旧执行隔离。下一笔任务开始后，向旧任务发送未见过的取消修订，收到拒绝，当前任务保持运行。

## 2026-11-26 正式续跑证据

正式续跑车队重启门控、rolling、whole_route 与暂停后取消专项均通过，已独立核对原始断言。完整在线恢复对账及整轮验收仍开放。 [原始证据独立复核](../../../docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/independent-review.json)

## 2026-11-26 同快照验收覆盖复核

候选 `58b9000-merged-f5c5d5bc` 的阶段 2 结果中，Qt 的 `replayHasNoOperations` 和 `remoteControlReconcilesOriginalTaskAfterReconnect` 通过。测试覆盖受理后显示 `PAUSING`，任务选择改为 B 后，断线重连仍查询 A 的执行标识和原修订，确认停稳后才显示 `PAUSED`。ROS 的 `RemoteControlPreservesRequestAndReceiptIdentity` 核对目标、执行标识、修订及回执，经同一快照单测通过。任务 3.1 完成，进度 3/5。[Qt 原始结果](../../../docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage2_unit_test/robot_console_core.ctest.xml) · [ROS 原始结果](../../../docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage2_unit_test/test_ros_operations.gtest.xml)

续跑阶段 7 保留 Ops 10/10、Alarm 3/3 对照。真实 Alarm 发布端下，rolling、whole-route、暂停后取消三个场景通过。复用重启标记时代理阻止控制，此项只覆盖门控。因此任务 1.1 的重启后任务与结果恢复对账仍开放。[续跑场景结果](../../../docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/stage7_console_smoke/task-control-scenarios/results.tsv) · [重启门控结果](../../../docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/stage7_console_smoke/task-control-restart/results.tsv)

任务 4.1 要求保存按目标关联的时间、位置窗口和资源持有者。目前断言已通过，存档日志只有场景结论，仍缺可逐项重建运动与路权变化的原始记录。整轮正式阶段 3 维持 109/110。

### 调度中枢重启时原命令查询

同一标记重复启动后，旧命令查询返回 `RECOVERY_REQUIRED` 并保留原任务标识、执行代次和修订号。列表报告待对账，新派单和无权威状态写入被拒。代理重启回归通过，五组任务包 CTest 通过。仅验证对账前的保护与答复语义；仍缺真实旧单、位姿、路权和回执的恢复。[双场景结果](../../../docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/restart-authority/after/results.tsv) · [CTest](../../../docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/restart-authority/ctest.log)

## 11 月 26 日取消驻留的在线重启对账

重新编译的合并代理与现有调度端、Alarm 一起进行隔离 ROS 验证。代理重启后保留原执行 ID 和取消修订，恢复停点坐标并维持驻留。调度端确认同车任务为 CANCELED，同一交通发布端仍列出原占用。显式移车后才驶离并释放原路段。反例在重启前释放原占用，代理报告 RECOVERY_REQUIRED，拒绝移动且未重新发布位姿。检查点单测通过 2/2，覆盖独占锁、读回、截断及作废。
[重启与路权原始轨迹](../../../docs/baseline/nightly/2026-11-26-task-control-isolated/task-control-scenarios/fleet-restart-held-trace.jsonl) · [路权丢失反例](../../../docs/baseline/nightly/2026-11-26-task-control-isolated/task-control-scenarios/fleet-restart-missing-route-trace.jsonl) · [检查点单测](../../../docs/baseline/nightly/2026-11-26-task-control-isolated/parked_task_checkpoint_test.xml)

任务 1.1 仍待活动任务跨进程重启对账，任务 4.1 待新轨迹版的正式共同验收。合并清单维持 3/5；原阶段 3 的 109/110 仍单独留证。

## 11 月 26 日正式阶段 3 复验

候选 `58b9000-merged-4965ab34` 的 110 个用例按登记表核对键、声明的运行栈和结果，全部通过。Gazebo 动态实体 270 秒，日志含装货确认和最终位姿断言。该作业仅运行阶段 3 和报告。当前更新后候选的完整共同验收另行运行。[110 项原始结果](../../../docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/stage3_acceptance/acceptance/results.tsv)

## 11 月 26 日已暂停活动任务重启复验

新版合并代理的检查点单测 3/3，隔离专项 10/10。rolling 和 whole-route 都验证了原任务 PAUSED、同一执行与原修订查询、停点位移不变及原占用保留。重启后继续驻停，急停中恢复仍被拒绝，显式恢复后在当前任务进度继续行驶。取消驻留、原路权丢失反例及旧请求隔离均通过。普通行驶段的恢复已实测，装卸恢复实现尚缺专门的重启场景。未停稳时中断、路权权威已更换及调度中枢重启后的任务索引恢复仍待核验或保持阻断。
[隔离轨迹和独立后验](../../../docs/baseline/nightly/2026-11-26-paused-restart-and-resume-feedback/independent-review.json)

装货停留补测在独立 ROS 域 223 通过。重启前保存 3.441 秒剩余停留，重启后不移动。显式恢复至第一次移动为 3.947 秒，期间作废旧检查点。测试使用站内停留和发车模型。[装货停留重启证据](../../../docs/baseline/nightly/2026-11-26-paused-restart-and-resume-feedback/dwell/result.log)

## 调度中枢重启后的分配与回执

在真实任务中枢两次进程启动间使用同一独占持久目录。隔离探针以测试代理确认原暂停回执，并保留可按同一标识返回另一条确认的原代理。重启后读回原任务与车队代理配属，已写入回执和原代理仍可核对的回执均以原执行与修订返回。未知修订保持待对账，恢复命令和可领取任务列表被阻断。检查点单测 2/2，四个隔离重启专项 4/4。结合此前真实车队代理在 rolling、whole-route 与装货停留的重启结果，任务 1.1 已核对，合并任务进度 4/5。本轮恢复的是原记录与可验证回执，重新允许调度前仍需实况路权核对。[重启专项后验](../../../docs/baseline/nightly/2026-11-26-mission-restart-reconciliation/independent-review.json) · [持久记录测试](../../../docs/baseline/nightly/2026-11-26-mission-restart-reconciliation/restart-store-test.xml)

## 11 月 26 日完整共同夜跑核验

修订 `58b9000-merged-59a37fbf`。冻结版八阶段全过，车队重启门控，rolling、whole_route、暂停后取消、取消驻留重启及丢路权反例均通过。冻结后新增活动暂停断点及中枢任务索引、回执恢复，继续待包含新版的 4.1 正式整轮。 [八例轨迹](../../../docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/stage7_console_smoke/task-control-scenarios/) [完整后验](../../../docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/postcheck.json)

2026-11-26 18:08（北京时间），新版冻结修订 `58b9000-merged-e8c4e462` 的 `merged-restart-reconciliation-full` 按用户指令取消。复核 VM 启动日志为 `Aborted`，编译 21/21、单测 695/695，阶段 3 只产出 23 条 PASS，未执行阶段 7 专项，未生成整轮报告和后验。两项 4.1 仍开放，已通过的隔离证据继续保留。补发终态已核对，候选继续暂停。[冻结源码清单](../../../docs/baseline/nightly/2026-11-26-restart-reconciliation-full-evidence/source.manifest)

## 2026-11-28 正式共同夜跑 4.1 通过（`0355FtYr69MW3ThcBLkzXp`）

修订 `8766f99-main-merge-u19-r2`（基于最新 `origin/main@8766f99` 冻结 1,016 个文件），于北京时间 2026-11-28 18:25:24 终态 **8/8 PASS**，本地复核 `source.manifest` 1,016/1,016 SHA-256 零偏差，`postcheck.json` 零问题（`"issues": []`）。阶段 7 保留 Ops 10/10 与 Alarm 3/3 对照全 PASS，重启专项 4/4（`fleet`、`dispatcher-state`、`d16`、`d16-state` 全 PASS），远程车队场景 7/7 全 PASS（`fleet-rolling`、`fleet-whole-route`、`fleet-cancel-paused`、`fleet-restart-held`、`fleet-restart-missing-route`、`fleet-restart-paused-rolling`、`fleet-restart-paused-whole-route` 及其完整轨迹采样均通过后验）。原始证据：[报告](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/report.md) · [后验](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/postcheck.json) · [源码清单](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/source.manifest)。任务 `4.1` 已核销（5/5）。

### 覆盖源码与脚本清单（verify-change 对齐）
- `src/robot_interfaces_fleet/srv/ControlRemoteFleetTask.srv`
- `src/robot_bringup/launch/fleet_sim.launch.py`
- `src/robot_console/src/ros_operations.cpp`
- `src/robot_console/test/test_ros_operations.cpp`
- `src/robot_console_core/include/robot_console_core/operation_source.hpp`
- `src/robot_console_core/src/operation_panel.cpp`
- `src/robot_console_core/src/operation_panel.hpp`
- `src/robot_console_core/test/test_operations.cpp`
- `src/robot_simulation/include/robot_simulation/parked_task_checkpoint.hpp`
- `src/robot_simulation/include/robot_simulation/task_stop_observation.hpp`
- `src/robot_simulation/src/fleet_sim_stop_node.cpp`
- `src/robot_simulation/test/parked_task_checkpoint_test.cpp`
- `src/robot_simulation/test/task_stop_observation_test.cpp`
- `scripts/check_task_control_remote_motion.py`
