# robot Dagu 工作流手册

## 安装路径与 `--dagu-home` 统一规范

- **Ubuntu VM**：Dagu `2.27.0` 固定安装至 `/usr/local/bin/dagu`（`0755`，非交互 SSH `PATH` 直接可见）。
- **Mac 宿主机**：Dagu `2.27.0` 固定安装至 `~/.local/bin/dagu`。
- **统一 `--dagu-home`**：所有运行与校验命令统一使用 `--dagu-home "$control_dir/dagu"`；读取端 `scripts/dagu_brief_remote.py` 永久保留 `("control/dagu", "control/state")` 双目录扫描。

## VM 夜跑工作流（`dagu/robot-nightly.yaml`）

在已有 ROS Jazzy、Bash、Python 3、`flock` 和 Dagu `2.27.0` 的 Ubuntu 测试主机使用：

```bash
export ROBOT_WORKSPACE="$HOME/robot_ws"
export DAGU_HOME="$HOME/control/dagu"
dagu validate --dagu-home "$DAGU_HOME" "$ROBOT_WORKSPACE/dagu/robot-nightly.yaml"
dagu start --dagu-home "$DAGU_HOME" "$ROBOT_WORKSPACE/dagu/robot-nightly.yaml" -- \
  "ROBOT_WORKSPACE=$ROBOT_WORKSPACE" DRY_RUN=1 ONLY_STAGES=8
```

夜间实跑时将 `NIGHTLY_REV` 设为本次被测版本（须对齐最新 `origin/main`）：

```bash
dagu start --dagu-home "$DAGU_HOME" "$ROBOT_WORKSPACE/dagu/robot-nightly.yaml" -- \
  "ROBOT_WORKSPACE=$ROBOT_WORKSPACE" DRY_RUN=0 NIGHTLY_REV='<本次被测版本>'
```

## Mac 跨工具派单工作流（`dagu/codex-apply.yaml`）

Mac 宿主机通过 `~/.local/bin/dagu` 运行 `dagu/codex-apply.yaml`，依次执行 `preflight`（`robot_guard` 账本/Worktree/7 段式施工单校验）→ `apply`（`codex exec -m gpt-6.2-sol -c model_reasoning_effort="high"`）→ `collect`（白名单 diff 校验、从 `sol-events.jsonl` 回填 `session_id` 并产出 `return-bundle.json`）。

> **安全边界定位**：`dagu/codex-apply.yaml` 与 `tools/codex_apply_helper.py` 定位为**受信任代理的范围防呆与 diff 审计门禁（非 OS 安全沙箱）**。白名单与 diff 核验会拒绝含 `..`、绝对路径、`.git/` 或解析后逃逸 worktree 根目录的符号链接，但不替代操作系统级隔离。

施工单七个部分按顺序用 Markdown 标题标记。第一节用 `worktree`、`branch`、`HEAD`、`change` 四个字段逐行声明执行环境（例如 `` - branch: `codex/example` ``），值用反引号包裹，`HEAD` 使用完整哈希。白名单逐条列出用反引号包裹的仓库相对路径。运行目录设在目标工作树之外。

```bash
control_dir="<本次派单目录>/control"
~/.local/bin/dagu start --dagu-home "$control_dir/dagu" \
  /Users/mima0000/CLionProjects/robot/dagu/codex-apply.yaml -- \
  "WORKTREE=$PWD" "CHANGE=<变更名>" "BRIEF_FILE=<施工单路径>" "RUN_DIR=<输出目录>"
```

## 查询进度和接收终态

主检出 `/Users/mima0000/CLionProjects/robot` 为宿主机 LaunchAgent 与 `scripts/dagu_chat.py` 的唯一权威运行入口。

```sh
python3 /Users/mima0000/CLionProjects/robot/scripts/dagu_chat.py bind \
  --adapter auto --thread "$THREAD_OR_SESSION" --worktree "$PWD" --change "$CHANGE" \
  --host "$VM_SSH" --workspace "$ROBOT_WORKSPACE" --output "$VM_OUTPUT"
python3 /Users/mima0000/CLionProjects/robot/scripts/dagu_chat.py status "$RUN_ID"
```

- `--adapter codex`（或默认 `auto` 检测到 Codex 线程 UUID）：通过本地 Codex daemon 队列恢复。
- `--adapter local`（或非 Codex 宿主如 Antigravity）：终态原子写入 `~/Library/Application Support/robot-dagu/inbox/<run_id>.json`（追加 `terminal-events.jsonl`）并发送 macOS 桌面通知，置 `done=True` 解锁归档清理。
