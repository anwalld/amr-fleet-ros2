# robot 项目工作规则

本规则适用于 robot 仓库根目录及所有共享其 Git common dir 的 worktree。接手前核对实际 worktree、分支、HEAD、未提交改动和变更的 `tasks.md`。后续命令显式使用对应 worktree。保留已有改动和未跟踪文件。

独立 worktree 集中在 `/Users/mima0000/CLionProjects/robot-worktrees/`，每个分支使用一个子目录。后续创建时也使用该目录，主检出仍在 `/Users/mima0000/CLionProjects/robot`（作为宿主机 LaunchAgent 与 `dagu_chat.py` / `robot_guard.py` 的唯一权威运行入口）。接续时用 `git worktree list --porcelain` 取得当前路径。

## 项目模式：托管（已启用）

用户于 2026-11-22 为整个 robot 项目启用托管模式，直到用户停用。其他项目规则和验收条件照常执行。

- 用户已常设授权项目范围内的夜跑。按 OpenSpec 的验证计划自行安排，无需逐轮询问。检查已有报告和进程使用只读操作。
- 每个新设计或实质性设计变更仍先走 `/opsx:explore`，记录依据、选项和结论。用户对托管期间的探索结论默认同意，可直接定稿并按验证流程推进。此前用户对具体方案的边界、禁止项和明确保留的决定继续有效。
- 托管授权覆盖夜跑和设计讨论。提交、推送和开 PR 等操作继续遵循对应变更的授权约定。
- 停用托管后，恢复每次夜跑单独授权，以及设计先经 `/opsx:explore` 与用户讨论确认后再定稿。

## OpenSpec 核验

- 每份 OpenSpec 设计文档都必须经过 `/opsx:verify`。设计评审时核对提案、设计、规格、任务及验证项的一致性，实施后完成包含实现证据的核验。按工作区的命令或对应 OpenSpec skill 执行，记录结论和待验证项。
- 实施核验须通过 `python3 /Users/mima0000/CLionProjects/robot/tools/robot_guard.py verify-change --worktree "$PWD" --change "<变更名>"` 核对 `git status` 全量修改/新增源码与脚本已被 OpenSpec 文档覆盖。
- 凡需共同夜跑整栈验收的变更，`openspec/changes/<name>/tasks.md` 必须单列末项「纳入基于最新主线的冻结快照并通过正式共同夜跑验收（核对 `source.manifest` 与零问题 `postcheck.json`）」，取得匹配全部改动哈希的正式产物前严禁勾选。

## 额度与接续（双宿主正交兼容）

- **Codex 环境**：
  - 仅在活跃工作开始、恢复以及长任务的阶段边界，调用 Codex 用量工具读取账号的 5 小时和周额度及重置时间（不设独立定时空轮询）。剩余为 100 减已用百分比。缺失数据记为未知。
  - 观察到 5 小时剩余严格小于 10% 或周剩余严格小于 5% 时，优先写交接，暂停新增工作。以所有触发阈值的窗口中最晚的重置时间为准，用 Codex 定时任务工具安排原对话在该时间接续。恢复时重查额度和 worktree，按交接继续已授权的工作。若重置时间或调度不可用，在交接和回复里说明。账号额度是共享的，已存在同一对话和重置时间的恢复任务时复用。
- **无配额 API 宿主（如 Antigravity）**：
  - 不执行未知额度空轮询，改为每完成一个独立验证闭环立即落盘更新 OpenSpec SSOT（`tasks.md`、`verification.md` 与缺陷账本）。
  - 遇宿主返回配额耗尽 / 429 错误时立即停机落盘；已知冷却时长时用宿主定时器排期一次，否则报告用户恢复。
- 交接沿用任务已有文档（`openspec/changes/<name>/tasks.md` + `verification.md` 辅以 `docs/handoffs/<name>-defects.tsv`）。没有 OpenSpec 变更时在当前 worktree 保存 `docs/handoffs/<变更名或任务名>.md`。记录任务目标和已确认决策、分支和 HEAD、工作区状态、已完成步骤及证据、待处理项、当前计数、运行位置和预计时间、授权边界以及恢复后的第一步。无证据的结果标为待核验。

## 后台任务复查

适用于 robot 项目的每次夜跑和其他后台作业。

1. 启动前根据历史用时和本次工作量估算完成时间，记录时区、估算依据、日志路径、作业标识和完成判据。ETA 到期时复查，运行期间保留每 30 分钟的巡检。
2. 到点检查报告、日志和进程，按结果处理。
   - **完成且通过**：核对完成判据，按已批准的变更设计继续执行下一步。
   - **仍在正常推进、与预估出入不大**：用已完成阶段和剩余工作量更新 ETA，重新安排到期检查。
   - **出现失败、没有进展或明显超时**：查看阶段耗时、日志和进程，分析原因。先确认故障和现有证据，再决定是否重试。
3. 记录实际检查结果、下一步或新 ETA。作业结束后关闭待执行的复查。

**当前实现状态**：Dagu 夜跑入口在作业内安排 ETA 到期检查和周期巡检，终态运行已配置的验收步骤。Mac 侧跨工具派单由 `dagu/codex-apply.yaml` 托管。

## 对话和缺陷计数（双宿主对等升级）

- 缺陷轮次记录在对应 worktree 的 `docs/handoffs/<变更名>-defects.tsv`，列为 `defect, round, outcome, evidence, introduced_defect`，用制表符分隔。只有证据表明旧缺陷已解决且本轮改动引入新缺陷，才记为 `regression`，并填写新缺陷标识。下一轮修这个新缺陷时，用该标识作为 `defect`。`fixed` 表示修复通过且没有已证实的新缺陷，`persists` 表示旧症状仍在，`pending` 表示待核验。每轮核验后运行 `python3 /Users/mima0000/CLionProjects/robot/tools/robot_guard.py check-defects <账本>`（或兼容入口 `codex_defect_count.py`），在开始下轮前检查 `escalate`。
- 项目 `.codex/hooks.json` 与 `.agents/hooks.json` 均调用 `tools/robot_guard.py` 统一守卫内核核对上下文压缩与缺陷链阈值。
- 上下文压缩按对话计数。达到 2 次即写交接并创建同项目的新对话，显式传入现有 worktree 绝对路径、分支和交接文档绝对路径，复用原 worktree 和未提交状态。
- **缺陷链熔断升级**：
  - **Codex 独立会话**：连续 3 次引入新缺陷（`escalate=true`）立即停下实现，切换至 `GPT-6.2 Astra`（`gpt-6.2-astra`，`high`）仅做诊断和设计，按项目规则经过 `/opsx:explore` 和 `/opsx:verify`，再由原模型执行。
  - **Antigravity 主控及跨工具协同流**：按 `Carbon > Neon > Hydrogen` 梯队运行，施工层（`Sol` 或 `Hydrogen`）触发 `escalate=true`、报 `blocked` 或连续 2 轮 `Neon Check` 未过时，回升至 `Gemini 4 Carbon (High)` 仅做只读诊断与重设计。

## Dagu 简报与分支归档

- 共同夜跑采用**持锁协调者角色制**，启动前更新主检出 [统一登记](docs/handoffs/nightly/shared-nightly.md)，明确冻结范围、专项判据与证据路径。每次冻结前必须执行 `git fetch origin main`，若 `origin/main` 已有新提交须先对齐最新主线并通过 `lint:interface_coverage` 与 OpenSpec `--strict` 校验。交接分类从 [索引](docs/handoffs/README.md) 进入。
- 统一使用 `--dagu-home "$control_dir/dagu"`，读取端保持 `control/dagu` 与 `control/state` 双目录兼容。
- 启动 Dagu 后从 Mac 按主检出 `dagu/README.md` 的 `bind` 命令把实际作业、当前 worktree、变更和发起对话（或 `--adapter local`）绑定。共同作业登记全部变更。用户询问时用 `dagu/README.md` 的 `status` 命令核对进度，直接在回复中给出各阶段通过数和北京时间 ETA。仅在 `/opsx:archive` 已成功移动变更后，在同一 worktree 执行以下收尾，替换实际归档变更名：

```sh
python3 /Users/mima0000/CLionProjects/robot/scripts/dagu_chat.py clean --worktree "$PWD" --change '<归档变更名>'
```

仅清理登记且已终结的同分支作业 `checks.jsonl`。共同作业须等所有登记变更归档。其他 JSON 验收证据继续保留。
