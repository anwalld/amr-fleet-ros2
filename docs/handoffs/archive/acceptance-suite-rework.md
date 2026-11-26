# 验收归档 CI 复验结果

2026-11-24（北京时间），按用户要求将 `anwalld/amr-fleet-ros2` 设为 public，以 `1428222` 撤回 PR #6 的免 CI 归档，再通过 [PR #7](https://github.com/anwalld/amr-fleet-ros2/pull/7) 重新归档。PR CI、分支 CI、撤销提交 CI 及 [最终 main CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45263502572) 均通过。PR 665 项测试零失败，归档 52/52，五份主规格已同步。

PR #7 的合并提交为 `f6312e4`。验收复验分支和 worktree 已回收。本地及远端 `main` 已同步至合入 M4-core 阶段 7 专项证据的 `58b9000`，主线 CI 665 项测试零错误、失败或跳过。专项六车 rolling 冒烟运行 300 秒，严格回放零跳行。 [PR #8](https://github.com/anwalld/amr-fleet-ros2/pull/8) · [主线 CI](https://github.com/anwalld/amr-fleet-ros2/actions/runs/45323716761) · [阶段 7 专项](../../baseline/nightly/2026-11-23-m4-core-stage7-c3b8861.md) · [缺陷账本](../acceptance-suite-rework-defects.tsv)
