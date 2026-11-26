# 11 月 26 日共同夜跑终态交接

`merged-control-current-full` 于北京时间 17:36:56 退出 0，冻结修订 `58b9000-merged-59a37fbf`。八阶段 8/8，独立复算冻结快照的 `shared-rework` 后验，六类检查均与 Dagu 产物一致、零问题。报告、巡检、源码清单和原始结果已同步到合并 worktree。VM 夜跑锁已释放，对应防睡眠断言已清理。[报告](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/report.md) · [后验](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/postcheck.json) · [巡检](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-26-current-candidate-full-evidence/checks.jsonl)

| 核验项 | 结果 |
| --- | --- |
| 标准回归 | 编译 21/21、单测 691/691、全量 110/110、演示 4/4。 |
| 压测与死锁 | 六组各采样 1800 条，最大间隔 1.03 秒，均零违规、无进程中途退出。四车 rolling 52 单，最长全队静止 4 秒。死锁 9/9，所需回放记录齐全。 |
| Ops 与 Alarm | Ops 10/10、Alarm 3/3。无人操作 300 秒仅三个读取服务，业务发布为零，外部封路后地图 169 毫秒更新。已存首份完整通过的 Ops 原始 TSV 和摘要。 |
| 任务控制 | 冻结版重启门控 2/2、专项轨迹 8/8。D16 和远程车队在冻结后继续实现，最新版本的隔离重启结果见任务控制交接。 |

Ops 14/14、Alarm 8/8、rolling 退避 3/3，已经回填并移出下轮专项候选。归档与合入按对应授权处理。D16 和远程车队均 4/5，下一轮需要将冻结后更改纳入，按新版代码与轨迹核对 4.1。标准八阶段回归照常保留。[Ops 基线](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/M4-ops.md) · [共同夜跑登记](shared-nightly.md) · [任务控制交接](../changes/task-control-restart-and-merged-nightly.md)

## M4 控制台交付接续

用户已明确授权交付 M4 控制台 Ops 和 Alarm。按 GitHub PR、CI 通过、合并到 `main`、OpenSpec 归档的顺序完成。归档变更同步到 GitHub 并核对主分支；若归档需要单独 PR，同样等待 CI 通过。GitHub 远端取工作树的 `origin`。不得强推。

- 复用 `/Users/mima0000/CLionProjects/robot-worktrees/merged-nightly-candidate`，分支 `codex/merged-nightly-candidate`。交接时 HEAD 为 `58b9000c4ecdc6e6c431c639828b315c012252e9`，与主检出当时的 HEAD 相同。有大量跨变更的未提交及未跟踪内容，保留全部。原控制台工作树为 `/Users/mima0000/CLionProjects/robot-worktrees/m4-console-ops-6389da`，分支 `claude/m4-console-ops-6389da`，交接时 HEAD 为 `d87cfa8b0926a80a74ccf51f8902807d38d324f1`，也有未提交修改，可用于核对归属。
- 首步重查额度、远端 `main`、两个工作树和 Ops、Alarm 的 `tasks.md`。交接时 5 小时余 91%，周余 42%。按通过版本的 `source.manifest` 核对代码和共享文件的变更，结合两份 OpenSpec 和原控制台工作树分离 Ops、Alarm 及必要依赖。可使用隔离交付分支，确保提交差异与通过证据对应。D16、远程任务控制、独立 rolling 变更、Dagu 本地工作资料和未完成复跑证据留在原工作区。
- 完整夜跑已通过，Ops 10/10、Alarm 3/3。误启动的 `merged-restart-reconciliation-full` 已按用户要求停止。交付使用已有通过证据和 GitHub CI。此交接尚无 PR、CI 或归档，接续先检查有无其他对话已创建对应 PR。
- 提交和合并信息保留 Claude Code 与 Codex 的共同作者署名。两份变更完成实现核验并在 OpenSpec 成功归档后，按项目规则执行对应作业的清理检查。共同作业须等其全部登记变更成功归档。
- 原对话已观测到两次上下文压缩。续接对话从实际 Git 状态及验收证据继续交付，并记录 PR、CI、合并提交、归档及清理结果。

### GitHub 交付进度

- 隔离工作树 `/Users/mima0000/CLionProjects/robot-worktrees/m4-console-delivery`，分支 `codex/m4-console-ops-alarm-delivery`，基于 `origin/main` 的 `58b9000`。[PR #9](https://github.com/anwalld/amr-fleet-ros2/pull/9) 的交付提交为 `701e8182457c8b020dbae43ae13771b5918029b0`。候选与原控制台工作树未改动。阶段 7 保存冻结快照的 Ops、Alarm 原始证据，交付提交由 GitHub Actions 验证。
- 重新核对 OpenSpec Ops 14/14、Alarm 8/8，严格校验通过。阶段 3 固定参考 110/110、接口覆盖 158/158、Ops 原始产物 10/10、Alarm 3/3、Mac Qt CTest 11/11。
- GitHub Actions 于北京时间 21:18 开始。[PR CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45827907215) 和[推送 CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45827838747) 的完成判据为 `ros2-ci / build-and-test` 成功。按最近主线运行约 14 分钟，预计 21:33 初查终态。在当前对话复查，无单次唤醒。PR 合并后仍须从合并提交创建归档分支并运行 CI。
- PR #9 的 PR CI 与推送 CI 均通过，PR 日志为 679 项零错误、失败和跳过，已于 21:30 合并。GitHub `main` 交付提交 `a4303041c36dfb561131180ad5c1fd53b6bd718f`。[合并后 CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45829786842) 于 21:30 启动。
- `codex/archive-m4-console-ops-alarm` 基于交付合并提交，同步 `operator-console`、`route-reservation`、`acceptance-validation` 后严格校验通过，已用 OpenSpec 移动两份变更并修正证据相对链接。归档提交 `1141c368dad06e036c2453033b82188a2e3c1a72`，[PR #10](https://github.com/anwalld/amr-fleet-ros2/pull/10)。[PR CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45830290299) 与[推送 CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45830191759) 于 21:32–21:33 启动，按 PR #9 实测约 10 分钟预计 21:44 完成，同样以 `ros2-ci / build-and-test` 成功为合并前提。
- 隔离归档工作树无匹配 Dagu 登记。归档同步主线后，原控制台工作树中按同分支登记核对 7 个终态作业。其余 7 个登记还含独立专项或未终结，共同跑次证据先保留。
- PR #9 合并后的 main CI 成功。PR #10 的 PR CI 和推送 CI 均成功，于 21:46 合并至 `fb10ae27374d9a64c04594a8a3f9292ad68a3176`。已取回远端主线，核对两份带日期的正式归档、原活动目录移除和三份同步的主规格。四条提交记录均有 Claude Code 与 Codex 共同署名。[归档合并后 main CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45832418627) 于 21:46 启动，预计 21:57 初查终态。
- 原控制台工作树的 Ops、Alarm 文档按主线完成项归档，历史资料随目录保留。Dagu 在原绑定分支清理 7 个已终结且仅登记 Ops 或 Alarm 的远端 `checks.jsonl` 及对应登记。共同作业 7 个登记仍在，已只读核对 `merged-control-current-full` 仍能返回 8/8；待其余关联变更归档后再按各绑定分支清理。其他验收证据和候选工作树仍保留。
- 21:58 复查最终 main CI，依赖安装通过，编译自 21:49:47 起仍在运行。按既往 main CI 约 14–16 分钟，续查 ETA 调整至 22:02。
- 22:02 复查，最终 main `fb10ae27374d9a64c04594a8a3f9292ad68a3176` 上的 [GitHub CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45832418627) 成功，交付和归档结案。共同夜跑登记继续随各独立变更处理。交付过程中未启动新夜跑。
