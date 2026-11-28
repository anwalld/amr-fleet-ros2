# OpenSpec 核验

## 首轮实施核验（任务 1.1–1.3，已完成）

| 场景 | 实施与证据 |
| --- | --- |
| 中断与并行构建 | 通过本分支 Dagu status 读取真实 Failed、Aborted、通过及 `control/state` 历史作业（兼容 `control/dagu` 与 `control/state`）。隔离记录验证在途重试和归档绑定守卫。四个脚本的清场正例和编译命令反例通过。 |
| 只读查询、夹具采样与导航退出 | Jazzy 编译并链接三个节点。定位夹具 3/3、读取助手 5/5、Gazebo 桥及跟随模式各通过延迟回执注入；两个 MarkerArray 夹具启用全文采样并通过语法与超时静态检查，`check_amr_sim_visualization.sh` 在 Jazzy 演示栈携带软件渲染变量实测通过。导航活动目标关闭 10/10、普通完成 1/1，两种 Nav2 假服务器的既有暂停或取消用例通过。 |
| 吞吐对照 | M4 和验收回查均以 122 为有效对照。两次停摆样本保留为带判废标记的历史记录。采集影响仍缺配对实测。 |

- 2026-11-26 21:20：基于已通过快照 `59a37fbf` 完成首轮 9 项修复隔离集成，任务控制轨迹后验 8/8 通过。
- 2026-11-26 22:55–23:01：补齐 U12 对 `control/state` 的原生终态兼容，并完成针对候选快照 `e8c4e462` 的隔离集成（三节点编译、6 项探针与导航回归、重启对账 4/4、过程轨迹 10/10 及 `amr_sim:visualization` 全部通过）。详见 [修复记录](../../../docs/handoffs/changes/repair-verified-defects.md) 与 [夜跑交接](../../../docs/handoffs/nightly/repair-verified-defects.md)。

## 任务 1.4 实施与隔离核验（2026-11-27，已对齐 `origin/main` `fb10ae2`）

- **代码与脚本覆盖范围**：
  - `src/robot_interfaces_mission/srv/RequestRouteSegment.srv`、`src/robot_interfaces_mission/srv/ReleaseRouteSegment.srv`、`src/robot_interfaces_mission/msg/TrafficGrant.msg`（二元租约字段，未触碰 `TrafficState.msg`，接口名零增删）
  - `src/robot_tasks/include/robot_tasks/mission_context.hpp`、`src/robot_tasks/include/robot_tasks/traffic_reservation.hpp`、`src/robot_tasks/src/traffic_reservation.cpp`、`src/robot_tasks/src/traffic_service.cpp`、`src/robot_tasks/test/traffic_reservation_test.cpp`（服务端 `service_epoch` + 单调 `resource_revisions` + `ApplyTrafficClearImpact` 一致性擦除 + 新增 `TrafficLeaseRevisionTest.StaleRevisionAndStaleEpochDoNotReleaseNewLock`）
  - `src/robot_tasks/src/navigate_sequence_server_node.cpp`（`U15`：`AttachLateCancelGuard` / `AbandonGoalWait` 与 `FollowWaypoints` 暂停瞬间 `SUCCEEDED` 结算保护，保留 D16 合流上下文）
  - `src/robot_simulation/CMakeLists.txt`、`src/robot_simulation/include/robot_simulation/agent_lease.hpp`、`src/robot_simulation/include/robot_simulation/agent_request.hpp`、`src/robot_simulation/src/amr_sim_gazebo_entity_bridge_node.cpp`、`src/robot_simulation/src/fleet_robot_agent_node.cpp`、`src/robot_simulation/test/agent_lease_test.cpp`（`U18`：v4 `AgentLeaseTracker` 活跃纪元/单调代次乱序过滤与 `(epoch, revision)` 精确释放核销、`RetryingRequest` 代次隔离、`ReportArrival` 5s 重发、在途释放屏障 `FlushPendingReleases`、`UpdateFleetState` / `UpdateTaskState` 确认重发，并完整保留 `origin/main` `fleet_sim_stop_enabled` 停车门禁）
  - `scripts/check_amr_sim_visualization.sh`、`scripts/check_amr_sim_default_demo.sh`、`scripts/check_localization_auto_recovery.sh`、`scripts/robot_wait.sh`、`scripts/run_acceptance_baseline.sh`、`scripts/run_deadlock_scenarios.sh`、`scripts/run_console_smoke.sh`（进程组 `setsid` + `-pgid` 回收、4 个 Mesa 软件渲染环境变量、`flock -n ~/.robot-nightly.lock` 门禁）
- **实测证据（VM 隔离工作区 `~/robot_ws_iso_vdf`，`ROS_DOMAIN_ID=217`，禁用全局 `ros_sweep`）**：
  1. 接口覆盖检查：`python3 scripts/lint_interface_coverage.py` → `源码接口 158 个，覆盖表 158 行，问题 0 处`；
  2. 跨包编译顺序：先 `colcon build --packages-select robot_interfaces_mission`（通过），再 `source install/setup.bash` 后 `colcon build --packages-up-to robot_tasks robot_simulation`（15 个包全部编译成功）；
  3. 单元测试：携带 4 个 Mesa 软件渲染环境变量运行 `colcon test --packages-select robot_tasks robot_simulation --ctest-args -R "agent_lease_test|traffic_reservation_test|traffic_clear_service_test|fleet_sim_stop_test"` → `Summary: 498 tests, 0 errors, 0 failures, 0 skipped`（含 `AgentLeaseTest.IgnoresStaleEpochAndRevisionNoticesAndAcksExactBatch` 与 `TrafficLeaseRevisionTest.StaleRevisionAndStaleEpochDoNotReleaseNewLock`）；
  4. P06 演示脚本实测与进程清理：`check_amr_sim_visualization.sh` 实跑通过（`amr simulation visualization passed`），退出后 `pgrep -af "[g]z sim"` 零残留；`check_amr_sim_default_demo.sh` 在 `ListMissionEvents`（`loaded 0 mission event(s)`）处报 `docking did not reach CHARGING in default demo`，失败退出后 `pgrep -af "[g]z sim"` 同样零残留；
  5. P07 夜跑锁门禁测试：持有 `~/.robot-nightly.lock` 且 `ROBOT_NIGHTLY_LOCK_HELD=0` 时调用 `scripts/run_acceptance_baseline.sh` 被立即拒绝退出（`无法取得 ~/.robot-nightly.lock，可能有其他测试或夜跑正在运行`）；
  6. 治理校验：`robot_guard.py verify-change`（`passed: true`, `uncovered: []`）与 `openspec validate repair-verified-defects --strict` 均通过。
- **待补验与待闭环项**：
  - 端到端动态探针待补验：U15 两项导航迟到取消/暂停瞬间成功定向探针、U18 丢首包 5s 重发与乱序释放端到端探针、以及 `check_amr_sim_default_demo.sh` 全程复验归入正式夜跑前/共同夜跑阶段 4 核验；
  - 任务 `1.5`：尚未进入基于最新主线的冻结快照正式共同夜跑，未取得正式 `source.manifest` 与零问题 `postcheck.json` 前保持未勾选。

## v4 合入基础设施后的差量复核（北京时间 2026-11-27 02:32）

- 阶段一已通过 PR #11 的 push 与 pull_request CI，主线及本工作树已对齐 `66a22f8`。两份 Mac 简报副本改用同一主线版本，原副本已备份。重新核对三份租约接口，`TrafficState.msg` 与主线字节一致。
- 外部会话的隔离证据保留。静态复核发现原客户端仍缺少淘汰纪元、逐资源水位线、等待期间通知仅提示以及完成回调消耗请求代次。已补齐上述差量，同时补齐工单锁租约分配、服务纪元初始化和客户端乱序用例。任务 1.4 恢复为待专项核验。
- 隔离目录 `/home/ubuntu/robot-dagu-workspaces/v4-verified-defects.sst9aE`，ROS 域 231、仅本机发现，编译日志 `probes/build.log`。两个包复编与定向测试预计 02:40 前，依据已有 Jazzy 编译安装产物、同机另有并发编译。完成判据为新增及相关现有单测、ROS 专项探针与锁门禁均通过。
- VM 已有另一会话启动的 `vdf-66a22f8-full` 持有正式夜跑锁，Dagu 初始 ETA 07:23。其源码早于本次 v4 差量，验收不能用于本分支当前 1.5。保持原作业与证据，待锁释放后才能冻结新版本或经用户授权处理。

### v4 专项实测（02:44，北京时间）

- Jazzy 定向复编 `robot_tasks` 与 `robot_simulation` 成功，服务端与客户端相关 CTest 6/6。服务端增加新旧实例均从 1 分配同一资源的确定性单测，旧实例释放跳过，新实例令牌正常释放。客户端单测覆盖已淘汰纪元、同纪元水位线、同代次重申与按完整令牌核销。
- 实际 `TrafficService` 与代理联调通过。注入旧纪元、旧代次通知后，代理仍使用真实新令牌成功释放；首次应答延迟后 5.051 秒重发。同纪元实际封路消息携带非零纪元并触发改道，旧纪元 `replan` 未改变路线。
- 车辆状态 5.048 秒重发、任务 `FINISHED` 5.050 秒重发、跟随模式到站 5.000 秒重发，迟到旧站回执未确认新站。
- U15 三个实际 ROS 动作探针通过。NavigateToPose、FollowWaypoints 都在取消截止后回收迟到句柄；FollowWaypoints 在暂停中结束成功，恢复后仍只有一次下发。探针发现无 NavigateToPose 的独立 FollowWaypoints 启动错误进入回退失败分支，调整条件，复编后通过上述验证。
- 三个全局清场入口在持锁探针中均返回 1，未调用清场。两个演示脚本的原 `start_bg` 与退出清理函数均通过失败注入，父子进程组无存活。既有 Gazebo 演示结果保留，默认演示 `CHARGING` 的全程仍由任务 1.5 正式阶段 3 验收。
- [专项原始证据及源码哈希](../../../docs/handoffs/repair-verified-defects-evidence/v4-targeted/)。22 个源码/脚本 SHA-256 在 VM 与本地逐项一致。1.4 已恢复勾选。1.5 保持未勾选，旧跑次自然终结后再独立冻结。

## 正式共同夜跑 1.5（北京时间 2026-11-27 12:31–17:50，未通过）

- `vdf-v4-3f1516e321-full` 基于最新主线 `66a22f8`，冻结修订 `66a22f8-v4-3f1516e321`。1,033 个文件的 SHA-256 在候选、实际 VM 源码与作业清单间一致。八阶段均已结束，5/8 通过。编译 21/21、单测 687/687、演示 4/4、死锁 9/9、Ops 10/10、报告 1/1。
- 阶段 3 为 109/110。唯一失败项 `amr_sim:gazebo_nav2`。Nav2 planner 配置完成后 `/planner_server/change_state` 回执发送超时，节点保持 inactive。同一冻结快照与安装产物在持锁隔离域定向复测通过。正式结果表保留原失败。
- 阶段 5 六组各 1,800 样本，最大间隙 1.08 秒，六组零路权违规。六车 rolling 第 1606.65 秒出现 robot_1、robot_2 无可达避让点的死锁兜底暂停，全队随后连续静止 193 秒，阶段判失败。证据不足以将该回归归因于本次租约改动，现有安全兜底及判据照常执行。
- 阶段 7 冒烟及回放通过、Ops 10/10、Alarm 2/3。失败项 `alarm_sim_stop` 已发现 SetBool 服务与状态话题，Qt 功能用例 120 秒超时，未写出有效 JUnit。同一冻结快照在持锁隔离域复测仍返回 124，卡点尚待确认。
- 失败跑次没有生成正式 `postcheck.json`。使用冻结版校验器另行复算，输出 `diagnostic-postcheck.json`，确认连续采样完整，且全量验收判据失败。当前 OpenSpec 4/5，1.5 未达到完整通过和零问题后验门槛。复测结束后 VM 夜跑锁已释放。原始运行与复测保留。[冻结与终态证据](../../../docs/handoffs/repair-verified-defects-evidence/v4-formal/)

## 1.5 接续探索（北京时间 2026-11-27）
- Alarm 对比正式和隔离复测的两次退出 124 与同冻结版开启 Qt 信号日志的一次通过。源码显示原确认 lambda 在 `trigger` 轮询按钮启用前入队，若这期间处理事件，确认动作可能在弹窗打开前被消耗。选定局部等待和有界应答，拒绝延长整项超时或删除真实窗口与停车断言。覆盖文件 `src/robot_console/test/console_ops_acceptance.cpp`。
- 六车 rolling 原录像的锁与等待图显示 robot_1 持有 jn_s、robot_2 持有 machining，且相互等待。停车安全兜底与停摆判据保持，先用同版本、同种子、同路网和 30 分钟窗口复测。VM 隔离域 222，日志 `/home/ubuntu/robot-dagu-workspaces/66a22f8-v4-3f1516e321/control/rolling-v4-retest.J1nnGn`，预计 19:50 前，依据组时长 30 分钟及启动清理预留。通过要求零违规、进程正常、1,800 个连续样本和停摆检查通过。
- Nav2 正式失败及同快照复测 PASS 原样保留。暂无依据修改生命周期行为或放宽判据。

- Alarm 夹具单文件差量在 Jazzy 重新编译，源码哈希本机与 VM 一致。三个全新 ROS 隔离域分别实测真实控制台取消/解除、下单、停车 1,200 毫秒、持锁及恢复，3/3 通过，单次 7.25–7.47 秒。生产控制台和停车逻辑无改动。[编译、JUnit 和服务原始记录](../../../docs/handoffs/repair-verified-defects-evidence/v4-followup/)
- 原六车 rolling 录像在触发暂停后只有六个车辆当前所在节点的占用，且一直持续到结束。此前路段与上一站的占用已经释放。robot_1 的直接出口被 robot_2、robot_3 与 robot_5 占用，robot_2 的直接出口被 robot_1 和 robot_6 占用，皆属于互等环或其上游等待车辆。当前证据支持路网饱和后的正常安全兜底，暂未观察到遗留路段租约。[原录像切片](../../../docs/handoffs/repair-verified-defects-evidence/v4-followup/frozen-rolling-pause.json)

- 首次六车 rolling 隔离复测 1,800 秒，15 单、零违规，压测驱动退出 0，但原停摆判据返回 1。录像中路权与快照最后一条约在 458 秒，逐车位姿最后一条约在 469 秒，压测 CSV 持续采样至末尾，全队静止 1,340 秒，兜底暂停次数为 0。此轮症状不同于正式跑次的无退路安全暂停，尚不能归因于二元租约。[首次隔离复测结果](../../../docs/handoffs/repair-verified-defects-evidence/v4-followup/rolling-retest/)
- 启动冻结版第二次六车隔离诊断，改用域 217 与默认发现设置。监测连续采集时间，遇到停更抓取正在运行的节点线程等待及系统调用。输出 `/home/ubuntu/robot-dagu-workspaces/66a22f8-v4-3f1516e321/control/rolling-v4-diagnostic.w0WMMm`，预计 20:24 前，依据 30 分钟窗口与启动清理。仍按停摆、连续采样、进程与违规判据验收。同日正式共同跑已完成，先用隔离证据得出诊断，再决定下一轮冻结。

- 第二次六车隔离诊断有完整的 1,800 个 1 Hz 样本，共完成 57 单、零路权违规、进程正常退出。采集持续，未触发停更诊断。第 1680.52 秒复现 robot_1、robot_2 相互等待且均无可达退路的原症状，任务中枢触发安全兜底，从第 1681 秒起全队静止 119 秒，现有停摆判据返回 1。[第二次隔离复测](../../../docs/handoffs/repair-verified-defects-evidence/v4-followup/rolling-diagnostic/)
- 续验结论：Alarm 局部修复通过 3/3，Nav2 同快照定向复测通过，六车 rolling 的正式失败模式已复现。保持 v4 已批准的零退路停车与正式验收阈值。1.5 仍未通过，未以孤立测试覆盖正式结果，未重新启动整轮。两个隔离作业已结束，VM 夜跑锁空闲。后续要改变该失败模式，需要独立设计避免形成无法解开的占用布局。

## 正式共同夜跑 1.5 通过（2026-11-28，`0355FtYr69MW3ThcBLkzXp`）

- 修订 `8766f99-main-merge-u19-r2`（基于最新 `origin/main@8766f99` 合流冻结 1,016 个文件），Dagu 作业 `0355FtYr69MW3ThcBLkzXp` 于北京时间 2026-11-28 18:25:24 终态退出 0，**八阶段 8/8 PASS**。
- 本地逐项复核 `source.manifest` 1,016/1,016 文件 SHA-256 零缺失零偏差，`postcheck.json` 零问题（`"issues": []`）。
- 实测摘要：编译 21/21、单测 724/724（0 errors/failures/skipped）、阶段 3 全量验收 **110/110**、阶段 4 一键演示 4/4、阶段 5 六组 30 分钟连续压测 6/6（每组 1,800 样本，最大间隙 <= 1.04s，0 违规，0 停摆；6 车 `rolling` 62 单 / 124.0 单/小时，0 `pause`，全队最长静止 5s）、阶段 6 死锁 9/9、阶段 7 冒烟 PASS + Ops 10/10 + Alarm 3/3 + 任务控制重启 4/4 + 任务控制场景 10/10、阶段 8 报告 PASS。
- 原始证据：[报告](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/report.md) · [后验](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/postcheck.json) · [源码清单](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/source.manifest) · [巡检](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/checks.jsonl)。任务 `1.5` 已核销（5/5）。
- 补充覆盖脚本（`verify-change` 对齐）：`scripts/check_amr_sim_gazebo_entities.sh`、`scripts/check_rest_workflow_controls.sh`。
