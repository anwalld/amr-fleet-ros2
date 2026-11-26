# M4-core 收尾交接

2026-11-24 12:20（北京时间）复核。主工作区为 `/Users/mima0000/CLionProjects/robot`，本地 `main` 与 `origin/main` 同在 `58b9000`。

| 检查项 | 最终状态 |
| --- | --- |
| OpenSpec 归档 | M4-core 31/31、验收重整 52/52。对该提交的干净副本运行严格校验，7 份主规格和 4 项归档全部通过。 [M4-core 归档](../../../openspec/changes/archive/2026-11-24-m4-console-core/tasks.md) · [验收重整归档](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/tasks.md) |
| 合并到 main | M4 实现 #3、验收重整 #4、M4 归档 #5、验收重整归档 #7、M4 阶段 7 证据 #8 均已合入。 [M4 实现](https://github.com/anwalld/amr-fleet-ros2/pull/3) · [验收重整](https://github.com/anwalld/amr-fleet-ros2/pull/4) · [M4 归档](https://github.com/anwalld/amr-fleet-ros2/pull/5) · [验收重整归档](https://github.com/anwalld/amr-fleet-ros2/pull/7) · [补测证据](https://github.com/anwalld/amr-fleet-ros2/pull/8) |
| 合并后 CI | `58b9000` 上的 `ros2-ci` 于 12:17:53 结束并通过。colcon 汇总 665 项，零错误、失败或跳过，含 core 的 10 项 CTest。Qt 6.4.2 已成功安装。 [CI 日志](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45323716761) |
| 分支与 worktree | `m4`、`codex/archive-m4-console-core` 和 `codex/m4-stage7-evidence` 的本地及远端分支均已删除，归档 worktree 已移除。未强推。M4-ops 工作区与历史分支仍保留。 |
| 文档统一 | M4 基线、M1–M3 验收回查、阶段 7 专项和六车 whole_route 专项均在 `main`。原共同夜跑报告保留。六车 whole_route 补测取得连续 1,800 条采样、完成 20 单、零违规；验收重整 11.2 已收尾。 [M4 基线](../../baseline/M4.md) · [M4 阶段 7 专项](../../baseline/nightly/2026-11-23-m4-core-stage7-c3b8861.md) · [验收回查](../../baseline/acceptance-rework.md) · [六车专项](../../baseline/nightly/2026-11-23-m4-core-rework6-r2-supplement-6-whole-route.md) |
| 临时文档 | M4 旧交接、缺陷账本和报告副本已移至废纸篓。工作区现在使用 `main` 上的正式报告。本页是新的本地收尾交接。验收重整旧交接及账本、Dagu 工作交接及账本仍在本地。 |

M4 阶段 7 现场复验的六车 rolling 冒烟完成 300 秒，严格回放零跳行，p95 为 32.71 ms、27.39 FPS。正式报告记录 VM 原始证据路径。 [阶段 7 实测](../../baseline/nightly/2026-11-23-m4-core-stage7-c3b8861.md)

本轮 M4-core 无待合入、待补测或在跑的 CI。主工作区仍有 Dagu 的未提交文件和 `scripts/nightly.sh` 改动。M4-ops 独立工作区 `/Users/mima0000/CLionProjects/robot-worktrees/m4-console-ops-6389da` 的 HEAD 为 `d87cfa8`，有进行中的改动。接续相应任务时先核对各自 worktree 和 HEAD，沿用其交接与授权。 [Dagu 交接](../nightly/robot-dagu-nightly.md)
