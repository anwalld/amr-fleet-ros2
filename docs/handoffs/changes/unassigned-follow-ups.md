# 车队项目待归属事项核对

远程车队任务控制和 D16 暂停协调已各自立项。合并候选已实现新版重启对账并通过隔离专项，两项均完成 4/5，剩余 4.1 为新版冻结快照的正式共同验收。[远程车队任务清单](../../../../robot-worktrees/merged-nightly-candidate/openspec/changes/remote-fleet-task-control/tasks.md) · [D16 任务清单](../../../../robot-worktrees/merged-nightly-candidate/openspec/changes/coordinate-mission-pause/tasks.md) · [任务控制交接](task-control-restart-and-merged-nightly.md)

立项前核对时，查到两项明确留给后续、尚未找到具体 OpenSpec 变更承接的工作：远程车队任务控制、任务中枢与导航层的暂停协调。另有一项 Gazebo 让路能力，原设计约定按需启动。

核对范围包括主线 `58b9000`、M4 Ops/Alarm 独立工作区（基于 `d87cfa8`，含未提交改动）和当时的 GitHub 开放 issue。以下判断依据代码、测试和已有验收记录，本次未重新运行仿真。

## 明确留给后续的两项工作

| 事项 | 已经做到哪一步 | 待承接的范围 |
|---|---|---|
| 远程车队任务的暂停、恢复、取消 | Ops 能按订单号控制工作流，也能提交远程搬运任务并按任务号跟踪状态。Ops 测试明确要求远程任务行没有控制入口。 | 操作员按远程任务号发起暂停、恢复和取消，并能核对目标任务的结果。Alarm 承接了告警、仿真车队急停、重连和安装，仍将远程任务控制留作独立工作。[Ops 任务 3.1–3.2](../../../../robot-worktrees/m4-console-ops-6389da/openspec/changes/archive/2026-11-26-m4-console-ops/tasks.md) · [Alarm 提案](../../../../robot-worktrees/m4-console-ops-6389da/openspec/changes/archive/2026-11-26-m4-console-alarm/proposal.md) |
| 任务中枢与导航层的暂停协调（D16） | 导航回退模式已支持暂停时取消目标、恢复后重发。任务中枢的 `SendControlRequest` 在服务未就绪时记日志返回，就绪时异步发出请求，没有读取应答。 | D16 明确要求另开变更，确定谁有权解除哪种暂停、如何确认请求及处理乱序、取消后下一任务是否继承暂停，以及如何验证车辆确已停下。这些问题即使在没有调度台参与时也存在。[D16 归档设计](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d16-暂停语义转独立变更撤回与已知问题) · [任务中枢控制请求](../../../src/robot_tasks/src/mission_runner_node.cpp) |

## 按需启动的一项能力

**Gazebo 任务中枢自驱车辆执行改道和退避。** 轻量仿真中的车队代理已订阅 `/traffic/yield`。任务中枢自驱的站点运输被选中让路时，会撤掉等待并按原路重申请，反复成环可触发兜底暂停。M3 设计把完整让路留到这类车辆确有需要时再立项。[M3 设计](../../../openspec/changes/archive/2026-11-19-m3-deadlock-detection/design.md) · [代理订阅实现](../../../src/robot_simulation/src/fleet_robot_agent_node.cpp)

## 已核对并从待归属清单移出的事项

| 事项 | 排除依据与仍有的边界 |
|---|---|
| 站点运输的上下货、导航结果和取消 | 这些状态转换已有实现，任务中枢已有调用。回填规格时留下的是转换函数的直接单测，现有工作流单测集中在提交入队。[转换实现](../../../src/robot_tasks/src/station_transport_workflow.cpp) · [直接单测](../../../src/robot_tasks/test/station_transport_workflow_test.cpp) |
| 车队请求超时和重发 | 代理对站点、路网、任务查询和路权申请已实现超时重发。释放路权、上报任务及车辆状态仍未核对应答；跟随模式的站点查询、车队查询和到站上报仍靠回调清除在途标志。D18 记录了这些可靠性缺口，未承诺另行实施。[代理及跟随模式实现](../../../src/robot_simulation/src/fleet_robot_agent_node.cpp) · [D18 范围](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d18-产品修复车队代理幂等请求丢应答时重发) |
| 环外上游车辆主动退让 | 现有规划会找出上游等待车辆，给环内车辆规划退路时避开它们占用的资源。让环外车辆主动先退是 M3 基线列出的可选改进。[死锁规划实现](../../../src/robot_tasks/src/traffic_deadlock.cpp) · [M3 已知限制](../../baseline/M3.md#已知限制) |
| 调整优先级用例偶发失败（D20） | 增加日志和用例期限后，第 4 轮复验通过。账本保留 `pending`：目标请求丢失仍是推断，根因及功能修复均未证实。目标受理确认与超时已在 D16 的后续讨论范围内。[缺陷账本](../acceptance-suite-rework-defects.tsv) · [D20 归档记录](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md#d20-夜跑-3-其余失败脚本修复测试隔离与取证) |
