# 共同夜跑登记

这是共同夜跑选项、冻结范围和通知归属的唯一登记入口。启动与现场查询使用 [Dagu 手册](robot-dagu-nightly.md)。具体实现和原始证据见 [任务控制交接](../changes/task-control-restart-and-merged-nightly.md)。

## 固定回归与各轮专项

每次完整共同夜跑含八阶段。Build、单测、110 项原验收、演示、2/4/6 车各两种路权模式（每组 30 分钟）、三种死锁场景各三遍、调度台冒烟与当轮专项、最终报告。启用 `shared-rework` 后验，验收阶段、采样连续性和各专项原始证据按冻结快照内的判据核对。专项从下一轮队列移出时，标准回归照常保留。

### 已停止的冻结跑次

- 作业 `merged-restart-reconciliation-full`，修订 `58b9000-merged-e8c4e462`，基准 HEAD `58b9000`。[源码清单](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-restart-reconciliation-full-evidence/source.manifest)
- 协调与 Dagu 绑定对话「M4-console-ops · Apply」，`01a20967-e0f2-71d1-92fc-1983a5a9db38`。沿用八阶段和标准回归；本轮新增核验以下两项。通过须同时有 110/110、六组连续压测、阶段 7 的四项重启及十项轨迹全 PASS，并经 `shared-rework` 后验。
- 原始结果 `/home/ubuntu/nightly/2026-11-26-merged-restart-reconciliation-full/`。进度运行 `python3 scripts/dagu_chat.py status merged-restart-reconciliation-full`。

| 变更 | 专项及原始判据 | 终态接收对话 |
| --- | --- | --- |
| `coordinate-mission-pause` | `d16` 与 `d16-state` 原目标 UUID、暂停、队列读回和禁止派发；迟到回执、危险中获授、运动切单的原始轨迹 | 查找孤岛未完成任务，`01a20d38-6d0b-7613-8fd8-4c15135f556c` |
| `remote-fleet-task-control` | `fleet` 与 `dispatcher-state` 原分配、回执和禁止未对账控制；滚动、整路、取消后驻留、缺路权反例和两种暂停断点的原始轨迹。装货剩余停留沿用已回填的隔离实测 | 查找孤岛未完成任务，`01a20d38-6d0b-7613-8fd8-4c15135f556c` |

Ops、Alarm 和四车退避使用已验的基线，保留八阶段入口内置回归。已确认缺陷修复已用上一轮通过快照完成隔离集成，保留在下轮候选。

2026-11-26 18:08（北京时间）按用户指令停止。Dagu 原生状态为 Aborted，夜跑锁已释放。已完成编译 21/21、单测 695/695，阶段 3 中止。后续优先按上一轮结果完成缺陷处理或归档合入；此跑次不自动重启。

### 最近完结的冻结跑次

- 作业 `merged-control-current-full` 于 11 月 26 日 17:36:56（北京时间）通过。源码 `58b9000-merged-59a37fbf`。使用合并候选独立 VM 快照。范围固定为八阶段、六组 30 分钟压测、死锁 9 遍和下表专项。[源码清单](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/source.manifest)
- 作业协调与 Dagu 终态绑定均为「M4-console-ops · Apply」，对话 ID `01a20967-e0f2-71d1-92fc-1983a5a9db38`。实时以 `python3 scripts/dagu_chat.py status merged-control-current-full` 查询。原始输出在 VM `/home/ubuntu/nightly/2026-11-26-merged-control-current-full/`。

| 变更 | 本轮要验的专项 | 提交范围和终态接收对话 |
| --- | --- | --- |
| `m4-console-ops` | 阶段 7 实际窗口 Ops 10 项，无人操作时服务边界，外部封路刷新；按变更判据核对 | M4-console-ops · Apply，`01a20967-e0f2-71d1-92fc-1983a5a9db38` |
| `m4-console-alarm` | 阶段 7 告警恢复、仿真停车及解除、安装入口 3 项 | M4-console-ops · Apply，`01a20967-e0f2-71d1-92fc-1983a5a9db38` |
| `coordinate-mission-pause` | 阶段 7 重启门控、迟到回执、危险中等待路权、暂停后切单运动轨迹 | 查找孤岛未完成任务，`01a20d38-6d0b-7613-8fd8-4c15135f556c` |
| `remote-fleet-task-control` | 阶段 7 重启门控，rolling、whole_route、暂停后取消，驻留恢复与路权丢失反例 | 查找孤岛未完成任务，`01a20d38-6d0b-7613-8fd8-4c15135f556c` |
| `rolling-backoff-home-preference` | 阶段 5 四车 rolling 的连续采样、停摆、占用违规及进程退出判据 | M4-console-ops · Apply，`01a20967-e0f2-71d1-92fc-1983a5a9db38` |

本轮快照只含表列场景。D16 与远程车队冻结后新增的重启对账实现已有隔离证据，列在下一轮候选。上一轮通过的 M4 Core、M1–M3 验收重整和旧退避路径修复没有单独登记。

### 下一轮候选

| 状态 | 变更或修复 | 预备范围或待满足条件 | 负责与接收对话 |
| --- | --- | --- | --- |
| 隔离通过，待冻结 | `repair-verified-defects`，`codex/verified-defect-fixes`，基准 `58b9000`；已验证基于 `59a37fbf` 的集成补丁 | 下一个可纳入的完整八阶段冻结版。阶段 3 核对定位恢复、机会充电、设施演示、导航暂停及取消、AMR Marker 用例。另保留活动目标关栈、延迟回执、清场匹配与 Dagu 原生终态定向证据。全量须 110/110、后验零问题，定向全部通过。隔离集成三节点已编译链接，专项及任务控制轨迹 8/8 通过。两份已冻结清单均未覆盖本分支。[验收范围与证据](../../../../robot-worktrees/verified-defect-fixes/docs/handoffs/nightly/repair-verified-defects.md) | 排查未闭环缺陷原因，`01a213d9-7c6a-7d93-b59a-9ed8366f77bd` |
| 暂停 | D16 恢复回执与中枢重启对账 | 验证 RESUMING 到 RUNNING、存活目标 UUID 的驻停确认、暂停来源和队列读回，以及未知时禁止派发。保留逐步日志与位姿。 | 查找孤岛未完成任务，`01a20d38-6d0b-7613-8fd8-4c15135f556c` |
| 暂停 | 远程活动任务暂停断点与中枢重启对账 | 验证 rolling 和 whole_route 的暂停续行、装货剩余停留、分配与原修订回执恢复；缺路权时仍拒绝恢复。保留位姿、任务状态与占用轨迹。 | 查找孤岛未完成任务，`01a20d38-6d0b-7613-8fd8-4c15135f556c` |

候选任务控制的 [十项隔离轨迹核验](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-paused-restart-and-resume-feedback/independent-review.json) 和[中枢重启专项](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-mission-restart-reconciliation/independent-review.json) 属于下一份快照的准备证据。两个变更当前均为 4/5，仍需对应新版的正式共同验收 4.1。

### 加入、冻结和移出

> [!IMPORTANT]
> **持锁协调者角色制与主线对齐铁律**：
> 1. **持锁协调者角色制**：解除对固定单一对话 ID 的强绑定，获授权取得 VM `~/.robot-nightly.lock` 并完成冻结的当前会话自动担任当轮协调者；原会话暂停或跨平台切换时，新会话核对锁与本登记表即可无缝接管终态复核。
> 2. **主线对齐铁律**：每次冻结前必须执行 `git fetch origin main`；若 `origin/main` 已有新提交，下一轮冻结快照**必须先对齐/合入最新 `origin/main`** 并通过 `lint:interface_coverage` 与 OpenSpec `--strict` 校验。仅历史故障复现实验允许显式标注「非最新 `main` 诊断跑次（不作交付基线）」豁免。

1. **加入。** 每个变更的负责人对话在候选表追加一行，填变更 ID、worktree、基准 HEAD 或待合源码清单、具体必跑阶段和用例、通过阈值、证据位置、准备状态、自己的对话标题和准确 ID。标题供人读，ID 用于投递。同一变更只留一条活跃登记，补用例时更新该行。协调者核对用例已接入且证据能落盘后标为「可冻结」。
2. **冻结。** 启动前由协调对话确定 run ID、不可变源码清单、锁和最终范围，把入选项复制一份到跑次表，并把所有变更交给 Dagu bind。并行对话在跑次启动后增加的需求写到下一轮候选。源、判据或参测范围变化时建新 run ID，保留此前记录。
3. **移出。** 实际专项及其约定后验通过，且证据回填到对应交接或 OpenSpec 后，标记「已验移出」并附跑次和证据。保留仍未覆盖的子场景。失败、跳过、中断或证据不全时保留，标记原因和下一次具体检查。撤回或合并的重复项记录理由和承接条目。冻结跑次的参测表不回写删项。
4. **清理。** 活跃队列只留下一轮可能执行的项目，已移出条目沉到本文件的终态和移出记录。删除 Dagu 巡检 JSONL 走项目归档守卫，须同一跑次已终结且所有已绑定变更均成功归档。报告和原始验收证据继续留在各自位置。

### 终态通知

- Dagu 按 run ID 向唯一协调对话自动投递终态。协调对话对照冻结表核对报告、原始日志、后验及项目授权。通过、失败和中断都要按实际终态处理。若简报和原生 Dagu 状态冲突，以原生状态和日志为准。
- 协调对话按冻结表列出的 ID 去重。自己的对话记录终态，其他接收对话各发一次。带 run ID、该对话负责的条目结果、证据链接及仍开放事项。接收方按自己负责的范围更新交接。把送达或待送状态记在本文件；发送失败时保留状态并重试。
- 当前登记提供人工分发名单。Dagu 的自动绑定仍只投给协调对话。进度按需查，跨对话投递在终态复核之后执行。

## 终态和移出记录

| 作业或范围 | 结论 | 通知 |
| --- | --- | --- |
| M4 Core、M1–M3 验收重整 | 已通过并归档，从专项候选移出。[归档交接](../archive/m4-console-core-closeout.md) | 见各归档记录 |
| `merged-control-stage3-recheck` | 阶段 3 / 报告 2/2，110/110，保留为阶段证据。[结果](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/report.md) | 已通知本对话 |
| `merged-control-current-full` | 八阶段 8/8；21 包、691 项单测、110 项验收、演示 4/4、压测 6/6、死锁 9/9、Ops 10/10、Alarm 3/3、重启 2/2、过程轨迹 8/8，后验无问题。[原始报告](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/report.md) · [巡检](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/checks.jsonl) | 协调对话已核对。任务控制对话为本表指定接收方，单独投递待授权。 |
| Ops、Alarm、rolling 停车位优先 | 当前冻结快照的专项已验并回填，移出下轮专项候选。Ops 14/14，Alarm 8/8，退避 3/3。标准回归照常，OpenSpec 归档与合入另行处理。[当前交接](current-full-closeout.md) | 同一协调对话 |
