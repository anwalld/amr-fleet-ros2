# 设计核验记录

按仓库 /opsx:verify 核对本变更。OpenSpec 的 status 和 instructions apply 均可解析 proposal、design、specs 和 tasks。严格校验结果见[共同核验记录](../../../../../robot/openspec/changes/explore-task-control-boundaries/verification.md)。

| 维度 | 结果 |
|---|---|
| 完整性 | 5 条新增或修改需求，8 个规格场景，实施清单 0/5。任务 1 至 4 分别承接新增需求，任务 5 承接修订的主规格及原场景回归。 |
| 正确性 | 已比对当前代码与目标行为。现有代码只提供部分基础，尚无这套变更的完整实现或专项运行证据。 |
| 一致性 | 已检查目标、执行端、状态转换、观测条件及验收边界；实现对设计的遵循、代码风格与运行阈值有效性待实施核验。 |

核对依据：pause_state.hpp 当前只区分中枢及反馈标记；mission_runner_node.cpp 的 SendControlRequest 发送后不消费应答；主规格已包含两种 Nav2 执行模式及原取消结果约定。

归档前阻塞项：完成 tasks.md 的五项任务并提交覆盖上述场景的原始证据。复验 D16 中仅有读码或交接依据的竞态，尤其是旧目标成功与取消交错、直接调用兼容入口和恢复时的运动。 当前结论只覆盖设计阶段。

## 本地实施核验 2026-11-24

工作区为 `/Users/mima0000/CLionProjects/robot-worktrees/coordinate-mission-pause`，分支 `codex/coordinate-mission-pause`，基线 `58b9000`。修改仍在工作区。VM 隔离目录为 `/tmp/robot-pause-tests.e0pzyd`。

| 已核对 | 证据 |
|---|---|
| 暂停来源分开记录，定位解除不清除人工暂停 | `pause_state_test.xml` 共 6/6，含反馈与暂停来源的组合 |
| 两种定位与人工暂停操作顺序 | `pause_integration.log`，隔离 ROS 域调用任务中枢与导航序列服务，核对拒绝回执和状态顺序 |
| 模拟运动暂停不消耗目标执行预算，暂停中取消不会再发非零速度指令 | `motion_integration.log`，以固定模拟里程计驱动速度控制，核对 3 秒暂停后仍可恢复和取消后的输出 |
| 两节点编译与链接 | 用 VM 现有 Jazzy 依赖和 CMake 参数在隔离目录编译并链接 `mission_runner_node` 与 `navigate_sequence_server_node` |
| 规格和文档结构 | 当前工作区 `openspec validate --all --strict` 10/10 |

代码还为主线和站点运输的导航回调增加了本地代次，避免回调把旧请求结果用于新目标。候选任务 1.1 和 3.1 部分实现，整项仍待验证。仍需将跨层控制绑定到目标代次、阻止直接恢复绕过仲裁、完成暂停中取消后下一单显式放行与重启对账、验证双导航模式和实际位移。现有导航端暂停位是全局状态，放行下一单依赖它与旧序列终态完成对账；本轮只保留通过上述局部检查的改动。两项产品变更的任务清单均保持 0/5。远程任务控制实施基线依赖 Ops/Alarm 合入。

## 第二轮本地实施核验 2026-11-24

Ubuntu Jazzy 隔离目录 `/tmp/robot-d16-local.swAwZQ`。两个新增接口已生成，任务中枢和导航序列节点已由当前工作区源码重编译并链接。

- `dispatch-hold.log` 验证默认的控制链携带导航目标 UUID 和修订号。直接调用旧恢复入口不能解除中枢暂停。目标不匹配和旧修订请求均被拒绝。
- `dispatch-paths.log` 验证人工暂停取消 T 后，普通队列、低电回桩和抢占任务均须显式放行。队首变化、错误队列序号及旧目标命令均被拒绝。匹配的凭据放行后新导航目标开始。
- `pause_sources.log`、`motion_budget.log` 复验两种定位与人工暂停解除顺序、暂停超过模拟执行预算后恢复，以及暂停取消后的速度指令。
- `nav2-mode.log`、`waypoints-mode.log` 使用既有假 Nav2 验收脚本验证兼容入口、目标取消和重发。其脚本副本只替换了被测导航节点的二进制路径。

任务 2.1 和 5.1 已核对。跨层命令已能确认和重试同一目标的同一修订，实际停稳需独立里程计验证。进程重启后的联机对账、丢应答取证、完整夜跑仍待执行。显式配置非默认旧暂停或取消服务名的部署继续使用对应兼容通道。

## 2026-11-26 正式续跑证据

正式续跑重启门控与 D16 迟到回执、等路权期间定位丢失专项均通过，已独立核对原始断言。完整重启在线对账与任务 4.1 的剩余验证仍开放。 [原始证据独立复核](../../../docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/independent-review.json)

## 2026-11-26 同快照验收覆盖复核

候选 `58b9000-merged-f5c5d5bc`。原跑阶段 2 共 688 项通过。第 3 阶段的 `ops:navigate_sequence_nav2_pause` 和 `ops:navigate_sequence_cancel` 分别在 12 秒和 17 秒通过。原阶段总结果 109/110。[原阶段 3 汇总](../../../docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage3_acceptance/acceptance/results.tsv)

续跑阶段 7 通过，迟到回执、等待路权期间定位丢失，以及复用重启标记均有结果。任务 1.1 同时有 D16 独立工作区的两种恢复顺序、直线里程计与提前恢复拒绝证据。导航序列源码与独立工作区一致，合并运行验证了等路权的控制边界。任务进度为 3/5。[续跑任务场景结果](../../../docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/stage7_console_smoke/task-control-scenarios/results.tsv) · [D16 独立核验](/Users/mima0000/CLionProjects/robot-worktrees/coordinate-mission-pause/openspec/changes/coordinate-mission-pause/verification.md)

任务 3.1 尚缺真实源状态的重启恢复对账，已通过的是重启时阻断，以及同一进程内的回执重试与拒绝旧结果。任务 4.1 尚缺按目标贯通两种导航、运动观测和取消切单的原始时间序列。续跑场景日志主要保留断言结论。原第 3 阶段的 Gazebo 单例通过复验，正式 109/110 结果沿用。

### 重启时实测外部路权拦截

增强夹具先在首启获取路权，使用同一标记再次启动并重放申请。原二进制仍授予 `route_node:receiving`；修复后 D16 回归确认申请不获授、不排队，查询和释放均报告待对账。同时复验派发和导航目标门控。真实旧目标、暂停来源和车辆位置恢复仍待对账。[原版反例](../../../docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/restart-authority/before/result-d16.log) · [修复验证](../../../docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/restart-authority/after/result-d16.log)

## 11 月 26 日连续证据补测

隔离 ROS 域 218 的 `d16-motion-cutover` 记录实际虚拟里程计、速度、目标 UUID 和控制回执。停稳窗口超过 1 秒，位移不超过 1 厘米，样本间隔不超过 0.2 秒。取消第一单后队列保持，匹配放行后才收到另一导航目标并观测到继续移动。两种导航模式的回归仍引用原同快照阶段 3 结果。D16 启动门控仍需活动导航目标及调度队列的完整恢复对账。此次是隔离验证，任务 4.1 等下一次正式接入该轨迹的共同跑。
[切单位姿与回执 JSONL](../../../docs/baseline/nightly/2026-11-26-task-control-isolated/task-control-scenarios/d16-motion-cutover-trace.jsonl) · [独立后验](../../../docs/baseline/nightly/2026-11-26-task-control-isolated/independent-review.json)

## 11 月 26 日正式阶段 3 复验

候选 `58b9000-merged-4965ab34` 的 110 个用例按登记表核对键、声明的运行栈和结果，全部通过。Gazebo 动态实体 270 秒，日志含装货确认和最终位姿断言。该作业仅运行阶段 3 和报告。当前更新后候选的完整共同验收另行运行。[110 项原始结果](../../../docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/stage3_acceptance/acceptance/results.tsv)

## 11 月 26 日恢复回执复验

给同一目标的恢复回执加延迟。旧二进制收到操作请求后首个新状态为 RUNNING，尽管应答仍在代理等待。修复版先进入 RESUMING，丢弃确认前的旧反馈，收到当前修订的恢复确认及导航反馈后才进 RUNNING。独立后验从两份 JSONL 对比状态序列；同批任务控制专项 10/10。活动目标与队列在任务中枢重启后的重建仍开放。
[隔离轨迹和独立后验](../../../docs/baseline/nightly/2026-11-26-paused-restart-and-resume-feedback/independent-review.json)

## 调度中枢重启恢复对账

新编译的合并任务中枢以配置好的持久目录运行。隔离试验中导航序列进程保持在线，任务中枢停止再启动，按真实 action 状态匹配旧 UUID，并收到提高修订的停驶确认。持久任务名、暂停来源和待派发队列完整读回，旧目标缺失时不推断完成。重启后新派发、新路权请求继续阻断。检查点单测 2/2，覆盖独占、轮换、完整队列和损坏记录。重启专项 4/4。任务 3.1 已核对，合并任务进度 4/5。正式第 4.1 项需在包含本轮改动的共同快照验收。[恢复目标和队列](../../../docs/baseline/nightly/2026-11-26-mission-restart-reconciliation/task-control-restart/d16-state/recovered-state.txt) · [重启专项后验](../../../docs/baseline/nightly/2026-11-26-mission-restart-reconciliation/independent-review.json)

## 11 月 26 日完整共同夜跑核验

修订 `58b9000-merged-59a37fbf`。冻结版八阶段全过，D16 重启门控、迟到回执、危险中获授与运动切单专项通过，原始轨迹均通过后验。冻结后新增中枢持久化、旧目标 UUID 与队列恢复和延迟恢复应答状态改动，继续待包含新版的 4.1 正式整轮。 [八例轨迹](../../../docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/stage7_console_smoke/task-control-scenarios/) [完整后验](../../../docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/postcheck.json)

2026-11-26 18:08（北京时间），新版冻结修订 `58b9000-merged-e8c4e462` 的 `merged-restart-reconciliation-full` 按用户指令取消。复核 VM 启动日志为 `Aborted`，编译 21/21、单测 695/695，阶段 3 只产出 23 条 PASS，未执行阶段 7 专项，未生成整轮报告和后验。两项 4.1 仍开放，已通过的隔离证据继续保留。补发终态已核对，候选继续暂停。[冻结源码清单](../../../docs/baseline/nightly/2026-11-26-restart-reconciliation-full-evidence/source.manifest)

## 2026-11-28 正式共同夜跑 4.1 通过（`0355FtYr69MW3ThcBLkzXp`）

修订 `8766f99-main-merge-u19-r2`（基于最新 `origin/main@8766f99` 冻结 1,016 个文件），于北京时间 2026-11-28 18:25:24 终态 **8/8 PASS**，本地复核 `source.manifest` 1,016/1,016 SHA-256 零偏差，`postcheck.json` 零问题（`"issues": []`）。阶段 2 单测 724/724、阶段 3 验收 110/110、阶段 7 重启专项 4/4（`d16`、`d16-state`、`fleet`、`dispatcher-state` 全 PASS）与任务控制场景 10/10（`d16-delayed-reply`、`d16-route-hazard`、`d16-motion-cutover` 及完整轨迹采样均通过后验）。原始证据：[报告](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/report.md) · [后验](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/postcheck.json) · [源码清单](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/source.manifest)。任务 `4.1` 已核销（5/5）。

### 覆盖源码与脚本清单（verify-change 对齐）
- `src/robot_interfaces_mission/srv/ReleaseMissionDispatchHold.srv`
- `src/robot_interfaces_mission/srv/SetSequencePause.srv`
- `src/robot_tasks/include/robot_tasks/mission_restart_store.hpp`
- `src/robot_tasks/launch/mission_runner.launch.py`
- `src/robot_tasks/package.xml`
- `src/robot_tasks/test/mission_restart_store_test.cpp`
- `src/robot_tasks/test/pause_state_test.cpp`
- `scripts/check_mission_restart_reconciliation.py`
- `scripts/check_task_control_delayed_reply.py`
- `scripts/check_task_control_motion_cutover.py`
- `scripts/check_task_control_route_hazard.py`
- `scripts/check_task_control_restart.py`
- `scripts/run_task_control_restart.sh`
- `scripts/run_task_control_scenarios.sh`
- `scripts/task_control_trace.py`
- `scripts/verify_task_control_trace.py`
