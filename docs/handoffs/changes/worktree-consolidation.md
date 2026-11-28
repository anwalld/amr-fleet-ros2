# robot 工作树迁移与当前进度

北京时间 2026-11-26。六个独立 worktree 迁到 `/Users/mima0000/CLionProjects/robot-worktrees/`，主检出仍在 `/Users/mima0000/CLionProjects/robot`。迁移前后核对了分支、HEAD、Git 状态、所有文件内容、权限位和符号链接。随后更新本地文档和 Dagu 登记。旧绝对路径有 Finder 隐藏的软链接，供历史会话和本地缓存打开原文件。

## 工作树目录

| 目录 | 分支 | 迁移时 HEAD | 用途 |
| --- | --- | --- | --- |
| [coordinate-mission-pause](../../../../robot-worktrees/coordinate-mission-pause/) | `codex/coordinate-mission-pause` | `58b9000` | D16 单独实现 |
| [remote-fleet-task-control](../../../../robot-worktrees/remote-fleet-task-control/) | `codex/remote-fleet-task-control` | `58b9000` | 远程车队单独实现 |
| [verified-defect-fixes](../../../../robot-worktrees/verified-defect-fixes/) | `codex/verified-defect-fixes` | `58b9000` | 九项已确认缺陷修复和集成证据 |
| [merged-nightly-candidate](../../../../robot-worktrees/merged-nightly-candidate/) | `codex/merged-nightly-candidate` | `58b9000` | 合并候选、已冻结版本和原始验收证据 |
| [m4-console-delivery](../../../../robot-worktrees/m4-console-delivery/) | `codex/archive-m4-console-ops-alarm` | `1141c36` | 已交付的 M4 归档工作树 |
| [m4-console-ops-6389da](../../../../robot-worktrees/m4-console-ops-6389da/) | `claude/m4-console-ops-6389da` | `d87cfa8` | M4 原工作树和绑定证据 |

恢复时在主检出运行 `git worktree list --porcelain`，用 `git -C <实际工作树> status --short --branch` 查看当时状态。新 worktree 使用同一个总目录，约定已写入 [AGENTS.md](../../../AGENTS.md)。

## 迁移验证

- 六个独立 worktree 共 6,994 个文件及符号链接。用 `git worktree move` 搬移后、更新路径前，内容哈希、权限位、符号链接目标、Git 状态、分支、HEAD 和 common dir 逐树一致。随后修改文档链接和路径说明。
- 扫描仓库与六个 worktree 的 Markdown 本地路径，并修复 13 条原有失效链接。原因分别为文档搬层后的相对路径、M4 已归档规格路径及独立探索规格交叉引用。Markdown 路径复查零失效。主索引保留旧交接入口。
- Dagu 主机上的 JSONL 和冻结源码保留原样。Mac 端 8 项登记已按新实际路径更新，逐条核对分支与身份，均可通过只读查询返回。已通过整轮用已安装的简报入口复核仍为 8/8。夜跑锁空闲。
- Mac Qt 原构建记录保存在 `/Users/mima0000/build/robot_console_core/.m4-ops-pre-worktree-move-20261126/`。在原构建入口用新规范源码路径重新生成并编译，退出 0，CTest 11/11。运行日志见 [LastTest.log](/Users/mima0000/build/robot_console_core/m4-ops/Testing/Temporary/LastTest.log)。
- Codex 对话的项目目录仍是主检出。历史消息与最近文件缓存中的旧路径通过 Finder 隐藏软链接兼容，所有旧路径已验证解析到新工作树。Linux VM 的程序软链接属于原始运行证据，应在 VM 上查看。

## 三大唯一事实来源（SSOT）

- **单变更进度与证据 SSOT**：各 worktree 内 `openspec/changes/<name>/tasks.md` + `verification.md`（辅以计数账本 `docs/handoffs/<name>-defects.tsv`）。
- **共同夜跑队列与终态 SSOT**：主检出 [共同夜跑登记与当前专项判据](../nightly/shared-nightly.md) + 跑次原始 `source.manifest` / `postcheck.json` / `report.md`。
- **未立项历史缺陷 SSOT**：主检出 [缺陷调查与证据](../defects/unconfirmed-bugs-diagnosis.md)。

其余交接入口统一见 [交接索引](../README.md)。
