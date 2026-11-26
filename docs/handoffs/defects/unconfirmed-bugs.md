# robot 全项目未确认缺陷

2026-11-26 起按编号记录当前存在性、已定位原因和仍缺的证据，见[逐项复核](unconfirmed-bugs-diagnosis.md)。以下保留 11 月 25 日盘点时的判断。

盘点时间：2026-11-25 北京时间。范围覆盖主线及 Git 登记的全部 5 个 worktree、4 份去重后的缺陷账本、现行和归档 OpenSpec、VM 可读的历史夜跑结果、相关 Codex 对话及 GitHub 开放 issue。GitHub 查询无开放 issue。本文以现场证据和当前代码为准，重复的账本记录合并。同一用例在不同场景的失败保留各自证据。

「根因未确认」表示已经观察到失败，现有材料仍不足以解释其成因。「待专项核验」表示已有历史报告或代码线索，当前候选缺少对应的边界证据。「待正式验收」表示已有定向通过，整轮结论尚未产生。后续状态以源账本、OpenSpec 和 Dagu 终态为准。

## 已观察到失败，根因或持续性未确认

| 编号 | 范围与现象 | 最新证据及未确认点 |
| --- | --- | --- |
| U01 | M1–M3／D20 `mission:reprioritize` 超时 | M1 曾因短任务太快、轮询漏掉状态而失败。验收重整第 3 轮是另一次超时，任务端显示已发出，导航端无开始执行迹象。「目标请求丢失」仍是推断，加日志后通过，未证明这一故障已解决。[缺陷账本](../acceptance-suite-rework-defects.tsv) · [归档 D20](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d20-夜跑-3-其余失败脚本修复测试隔离与取证) |
| U02 | M0–M2 定位自动恢复不稳定 | `safety:localization_auto_recovery` 曾因协方差 1.2 超出 0.5 门槛而失败。M0 A/B 2/5、M1 7/10。M2 专项 3/3，后续正式基线通过。协方差为何未及时收敛及其是否还会出现待确认。与 Ops/Alarm 的重定位发布夹具问题分开记录。[M1 基线](../../baseline/M1.md) · [M2 基线](../../baseline/M2.md) |
| U03 | M0–M2 对桩重试不稳定 | `mission:docking_retry` 在 M1 原样重跑 1/3、M2 2/3，后续自起栈基线通过。早期偶发失败缺根因闭环，与回充命令应答丢失是不同用例。[M1 基线](../../baseline/M1.md) · [M2 基线](../../baseline/M2.md) · [现行基线](../../baseline/acceptance_baseline.tsv) |
| U04 | M1–M2 设施工作流偶发超时 | `business:scenario_facility_workflow` 历史超时后原样专项 3/3，正式基线通过。已有耗时和负载线索，具体阻塞环节未确认。[M1 基线](../../baseline/M1.md) · [M2 基线](../../baseline/M2.md) |
| U05 | M2 Gazebo 设施演示门板位姿未及时更新 | `facility:showcase` 曾失败，后续 3/3 和正式基线通过。历史报告指向软件渲染栈的位姿更新慢，缺少独立取证来闭环该归因。[M2 基线](../../baseline/M2.md) · [现行基线](../../baseline/acceptance_baseline.tsv) |
| U06 | DDS 服务回执偶发超时，横跨 M2、Ops/Alarm r2 和合并候选 | M2 在取消队列、行业工作流和路权场景记录到发送回执超时。Ops/Alarm r2 回充命令已执行，回执超时，持久客户端专项通过；合并首轮 `business:fleet_dispatch` 的 `/v2/update_fleet_robot_state` 服务端也记录发送回执超时，后续原判据复验通过。三个时期都有发送异常证据，是否共享同一根因仍未知。[M2 基线](../../baseline/M2.md) · [Ops 账本](../../../../robot-worktrees/m4-console-ops-6389da/docs/handoffs/m4-console-ops-defects.tsv) · [车队派单原栈](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__business__fleet_dispatch.log) |
| U07 | M4 Ops 机会充电设施查询超时 | 11 月 24 日原跑 `safety:opportunity_charging` 调用 `/v2/list_facility_resources` 30 秒无应答。同快照原样复查 PASS，34 秒。该次没有充分证据归因到 U06。[Ops 交接](../../../../robot-worktrees/m4-console-ops-6389da/docs/handoffs/m4-console-ops.md) |
| U08 | 合并首轮 `safety:route_locks` 模拟导航未完成 | 已受理目标后未取得预期 FINISHED。栈有速度看门狗告警，缺少同时段速度、里程计及复用器状态来判定因果。修复候选按原判据 55 秒通过。[原始栈日志](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__safety__route_locks.log) |
| U09 | 合并首轮 `ops:operator_snapshot` 模拟导航未完成 | 目标已受理，模拟运动耗尽预算，预期任务未 FINISHED。修复候选按原判据 41 秒通过。与 U08 是否同源未知，关栈崩溃另见 U10。[原始栈日志](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__ops__operator_snapshot.log) · [定向结果](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-hardening-evidence/stage3/results.tsv) |
| U10 | D16／导航序列节点正常关栈时异常退出 | D16 归档曾记下该现象。11 月 25 日操作快照原栈在 SIGINT 后实录反馈 publisher 已失效，抛出 `RCLError`，节点以 -6 退出。发生在验收目标未完成之后；仍缺带活动目标的关栈专项及修复证明。[原始栈日志](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__ops__operator_snapshot.log) · [当前发布路径](../../../../robot-worktrees/merged-nightly-candidate/src/robot_tasks/src/navigate_sequence_server_node.cpp) |
| U11 | D16 和合并候选编译被 SIGKILL | D16 在独立工作区曾被杀，空闲 VM 上重编通过。合并候选另一次被杀与补跑清场时间重叠，持锁串行编译及后续冷构建均通过。缺少信号发送者证据，尚不能确认两次同源或分别由何处触发。[构建交接](../changes/task-control-restart-and-merged-nightly.md) |
| U12 | Dagu 遇到宿主重启后仍显示作业运行中 | 原 `merged-control-nightly` 的 Dagu 直接状态为 Failed、进程失效。巡检缺终态行，结构化简报仍显示「运行中」，该路径无法触发终态投递或按登记终态清理。读取端当前仅靠 JSONL 终态判断。此次重启本身属于外部中断，缺陷是中断后的作业状态识别。[Dagu 原状态](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/dagu-status.txt) · [简报判断](../../../scripts/dagu_brief_remote.py) |
| U13 | M4 阶段 5 吞吐对照采用已判废样本 | 主线 M4 用 66、116 单/小时作为历史区间的一部分，M3 已判这两轮停摆。用原表两次有效的 122 对照，11 月 23 日实测 112 低 8.2%，触发当时原判据。第 4 轮开采集也达到 122，后来的若干六车记录有 120–128。现有证据不足以把那次差额归因于采集。PR #8 的阶段 7 不覆盖该问题。[M3 基线](../../baseline/M3.md) · [M4 对照](../../baseline/M4.md) · [验收回查](../../baseline/acceptance-rework.md) |
| U22 | 正式合跑阶段 3 的 Gazebo 动态货箱未移动 | `amr_sim:gazebo_entities` 原跑在 81 秒失败，货箱到限仍为初始位姿；11 月 26 日同快照原判据单例 250 秒 PASS，根因未确认。续跑第 5–8 阶段已通过，原 109/110 保留。[原失败](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage3_acceptance/acceptance/amr_sim__gazebo_entities.log) · [专项复验](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/gazebo-entities-recheck/acceptance/results.tsv) 正式阶段 3 复验全量 110/110，Gazebo 270 秒通过。[正式复验](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/stage3_acceptance/acceptance/results.tsv) |

## 已知可靠性缺口与待专项核验的历史问题

| 编号 | 范围 | 已有证据及剩余边界 |
| --- | --- | --- |
| U14 | D16 重启后任务与导航权威状态对账 | 持久标记复用时任务中枢拒绝派发、导航拒绝新目标已验证。重启后的旧目标、暂停来源、车辆位置、队列及路权的完整对账待实现与实测。补跑的重启门控通过仅覆盖前者。[D16 独立核验](../../../../robot-worktrees/coordinate-mission-pause/openspec/changes/coordinate-mission-pause/verification.md) · [合并交接](../changes/task-control-restart-and-merged-nightly.md) |
| U15 | D16 未闭环的目标生命周期竞态 | 归档列出 FollowWaypoints 取消与成功相撞、导航受理超出取消等待窗口、任务切换时迟到暂停或取消。已有双模式隔离验证及同修订迟到应答验证；仍缺与这些精确时序逐项对应的原始断言。候选等待 Nav2 受理时在取消后最多再等 2 秒，超出窗口的目标收敛没有闭环证据。旧无目标标识 Trigger 的迟到意图歧义记录在设计中。服务取消在 action 端以 ABORTED 返回属于现行兼容约定。[D16 归档](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d16-暂停语义转独立变更撤回与已知问题) · [现行设计](../../../../robot-worktrees/coordinate-mission-pause/openspec/changes/coordinate-mission-pause/design.md) · [候选实现](../../../../robot-worktrees/merged-nightly-candidate/src/robot_tasks/src/navigate_sequence_server_node.cpp) |
| U16 | D16 停车证据和栈间保护边界 | 历史记录称带速度闸门的栈有避障兜底，`fleet_sim` 缺同等闸门。已有直线仿真里程计与定位丢失获授路权场景通过。两种真实导航模式下的位姿与速度旁证、陈旧或丢测量时的停稳结论、各栈边界仍须按 D16 设计逐项核验。[D16 归档](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d16-暂停语义转独立变更撤回与已知问题) · [D16 核验](../../../../robot-worktrees/coordinate-mission-pause/openspec/changes/coordinate-mission-pause/verification.md) |
| U17 | 远程车队重启后的驻留、占用和结果对账 | 重启时进入 `RECOVERY_REQUIRED` 已实测。同 ID/修订重试、暂停后取消、两种路权模式已获专项证据。实际停点、占用、任务代次和请求历史的重建，以及运维清占用与驻留车辆交互，仍缺完整证据。Ops 断线后的原请求查询已有局部证据，跨进程完整对账仍开放。[远程车队核验](../../../../robot-worktrees/remote-fleet-task-control/openspec/changes/remote-fleet-task-control/verification.md) · [合并交接](../changes/task-control-restart-and-merged-nightly.md) |
| U18 | D18 车队代理剩余应答确认缺口 | 四种幂等请求超时重发已验证。释放路权、上报任务和车辆状态的应答未核对，跟随模式仍有三类在途标志仅靠回调清除。丢包后可能留下占用、旧状态或长期在途，当前缺这几条链路的定向证据。[D18 归档](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d18-产品修复车队代理幂等请求丢应答时重发) · [遗留核对](../changes/unassigned-follow-ups.md) |
| U19 | M3／D17 让路兜底场景 | 历史六车 rolling 有全部可用出口被上游占据、触发暂停后全队停摆的现场；已有多轮后续通过。Gazebo 自驱站点车被选中让路时只撤等待、再按原路申请。D17 另记只有封路可走时重试期间不处理新让路指令。分别属于拓扑零可用退路、车辆执行能力和代理重试边界，不能用四车停车位专项替代。[M3 已知限制](../../baseline/M3.md#已知限制) · [D17 归档](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d17-产品修复车队代理让路后越界与永久无路) |
| U20 | M3/M4 压测证据盲区 | rolling 判据能发现全队连续静止或单车全程零单；一辆车完成过单之后长期卡住、其余车持续跑，可能仍过关。1 Hz 交通状态也可能漏同秒的逐次死锁细节，累计计数仍在。D20 另记默认截断的 `topic echo` 调用未逐处排查，已知 RViz 标签夹具已修。[停摆判定](../../../scripts/stress_stalls.py) · [M3 基线](../../baseline/M3.md) · [D20 归档](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d20-夜跑-3-其余失败脚本修复测试隔离与取证) |
| U21 | Dagu 空闲对话唤醒和真实归档后清理待验 | 历史失败通知已投递，同快照恢复作业的绑定也已记录终态发送。仍缺一条独立证据证明空闲对话成功自动续步，以及共同作业所有登记变更归档后按守卫自动清理。历史已归档日志手动清过 5 份。[Dagu 任务 5.3](../../../openspec/changes/robot-dagu-nightly/tasks.md) · [Dagu 交接](../nightly/robot-dagu-nightly.md) |

## 已有定向通过，待正式验收的缺陷修复

- 四车 rolling 再次停摆。原合并跑全队静止约 1103 秒；停车位优先修复的 30 分钟复验通过，54 单、零违规、最长全队静止 6 秒。11 月 26 日同快照正式续跑阶段 5 与采样后验已通过，四车 58 单、零违规、最长静止 4 秒，该压测缺陷已收口。11 月 24 日首轮的旧退避路径修复已在 r2 通过，本条跟踪的是后续复发。[滚动修复核验](../../../../robot-worktrees/merged-nightly-candidate/openspec/changes/rolling-backoff-home-preference/verification.md)
- 回充及重定位夹具。回充持久客户端专项 3/3，传输层根因仍见 U06；重定位改为订阅匹配并可靠交付后专项 3/3。首轮故障监控和旧接口审计曾在 r2 通过，合并候选再次出现后已分别用持续订阅及新版接口复验通过。合并新增接口覆盖 162/162，REST 隔离域与日志修复用例通过；原 REST 超时缺内部步骤记录，具体阻塞点未确认。[Ops r2 与复验](../../../../robot-worktrees/m4-console-ops-6389da/docs/handoffs/m4-console-ops.md) · [合并缺陷账本](../../../../robot-worktrees/merged-nightly-candidate/docs/handoffs/merged-control-defects.tsv)
- 全栈验收仍以 `merged-control-repair-nightly` 的 `report.md`、`postcheck.json` 和原始阶段输出为准。补跑 6–8、Ops 10/10、Alarm 3/3、重启门控 2/2 和任务控制仿真 5/5 已有各自证据。Ops/Alarm 的完整验收项及 D16、远程车队的对账缺口独立跟踪。[补跑复核](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-recovery-evidence/independent-review.json) · [跑次入口与路径](../changes/task-control-restart-and-merged-nightly.md)

## 历史结果对照

M0–M3 中已经由归档用例确认的装卸夹具、链式工单、D17 越界、D18 四类幂等请求、D20 仿真串话、D21 断言完整性，按各自的通过证据归档。11 月 23 日六车 whole_route 采样缺口已由同快照完整补测覆盖，M4 回放零跳行已有阶段 7 专项。已确认的修复只在上文有复发、另一个根因未闭环或仍缺正式复验时列出。[验收重整回查](../../baseline/acceptance-rework.md) · [M4 Core 收尾](../archive/m4-console-core-closeout.md)
