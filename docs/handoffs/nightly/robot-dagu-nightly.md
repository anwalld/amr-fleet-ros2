# Dagu 夜跑启动交接

## 接手入口

- 从 robot 仓库根目录读取 [项目规则](../../../AGENTS.md)。用 `git worktree list --porcelain` 找到本次 worktree，用 `git rev-parse HEAD` 和 `git status --short --branch` 核对当前版本与未提交改动。待办和范围读该 worktree 的 `openspec/changes/<变更名>/tasks.md`，保留已有改动。
- 已归档的 M4-core 和验收重整见 [收尾交接](../archive/m4-console-core-closeout.md)及其基线和专项报告。进行中的任务从对应交接找到测试主机、待测快照和作业入口，再到 VMware 核对当前 VM 地址。连上后用 `hostname` 确认主机身份。
- 到 VM 先用 `command -v dagu` 查可执行文件。便携版可在 `~/robot-dagu-workspaces` 下找 `control/dagu-bin`，具体用 `find "$HOME/robot-dagu-workspaces" -path '*/control/dagu-bin' -type f`。选定后用 `version` 和 `validate` 验证。
- 当前作业从对应交接定位，到 VM 的作业启动日志、进程列表和结果目录确认。Dagu 作业看 `checks.jsonl` 最后一条和 `nightly.log`，专项作业看其交接列出的退出标记和原始数据。启动前核对 `~/.robot-nightly.lock` 和未纳入该锁的 ROS、Gazebo 进程。
- 调用 Codex 用量工具查看即时额度和重置时间，从本对话日志核对压缩次数。Dagu 缺陷账本查 [Dagu 缺陷记录](../robot-dagu-nightly-defects.tsv)，业务缺陷账本查本次 worktree 的 `docs/handoffs/`，用项目计数器核对 `escalate`。

## 准备新版测试工作区

1. 根据本次任务选定待测 worktree 和测试范围。从指定版本生成独立 VM 工作区，记录源码、未提交改动和接入文件的校验清单。复用已有快照时，先到其工作区检查源清单和接入文件；用本节清单及 dry-run 确认接入包含 ETA 检查和证据验收。
2. 从主检出的新版接入同步 `dagu/robot-nightly.yaml`、`scripts/dagu_nightly.py`、`scripts/dagu_nightly_evidence.py`。启用缺陷账本时同步 `tools/codex_defect_count.py`、`tools/codex_compaction_hook.py` 及对应工作区的账本。同一份快照须包含任务实际使用的验收脚本。保留其 `scripts/nightly.sh` 的阶段判据；若缺排他锁和退出清理，只适配该段，经补丁位置检查及 `bash -n` 再使用。参考主检出的 `git diff -- scripts/nightly.sh`。
3. 从 Mac 归档时使用 `COPYFILE_DISABLE=1` 和 `tar --no-mac-metadata --no-xattrs`。核对归档路径与源清单，Linux 解包后核对路径和 SHA-256，确保没有 `._*`。先跑 Dagu `validate` 及 `DRY_RUN=1 ONLY_STAGES=8`，确认 `checks.jsonl` 终态 `dry_run_passed`。完整共同验收的 `ROBOT_POSTCHECK=shared-rework` 在正式全阶段启动时传入。
4. 正式启动前重新确认 VM 无冲突作业、宿主接电并开盖，按约定启用防空闲睡眠断言。以当前工作区账本的计数结果处理待交接项。已有作业和预检输出用只读检查。

## VM 启动命令

在本轮确认的测试主机上，把 `DAGU_BIN` 设为核验过的可执行文件路径，`ROBOT_WORKSPACE` 设为本轮待校验的绝对工作区，`RUN_ID` 设为新作业标识。正式命令对应 M4-core 与验收重整共同全量复验。普通夜跑按任务设置范围、验收配置和账本。

```bash
: "${DAGU_BIN:?请填写已核验的 Dagu 路径}"
: "${ROBOT_WORKSPACE:?请填写待测 VM 工作区}"
: "${RUN_ID:?请填写新的作业标识}"
dag_file="$ROBOT_WORKSPACE/dagu/robot-nightly.yaml"
control_dir="$(dirname "$ROBOT_WORKSPACE")/control"
mkdir -p "$control_dir"
"$DAGU_BIN" --dagu-home "$control_dir/dagu" validate "$dag_file" || exit 1

"$DAGU_BIN" --dagu-home "$control_dir/dagu" start --run-id "$RUN_ID-preflight" "$dag_file" -- \
  "ROBOT_WORKSPACE=$ROBOT_WORKSPACE" DRY_RUN=1 ONLY_STAGES=8 || exit 1
```

预检成功后，重新核对锁、宿主和本轮授权，再在同一 shell 发起正式夜跑。全阶段是默认范围。

```bash
(
  set -o noclobber
  : "${NIGHTLY_REV:?请填写被测快照标识}"
  TZ=Asia/Shanghai nohup setsid "$DAGU_BIN" --dagu-home "$control_dir/dagu" start --run-id "$RUN_ID" "$dag_file" -- \
    "ROBOT_WORKSPACE=$ROBOT_WORKSPACE" DRY_RUN=0 "NIGHTLY_REV=$NIGHTLY_REV" STRESS_MINUTES=30 \
    ROBOT_POSTCHECK=shared-rework "ROBOT_DEFECT_LEDGER=docs/handoffs/acceptance-suite-rework-defects.tsv" \
    > "$control_dir/$RUN_ID-launch.log" 2>&1 < /dev/null &
  printf 'Dagu launch PID %s\n' "$!"
)
```

SSH 断开后重连。确认对应 Dagu 与夜跑进程、正式 `nightly.log`、计划中的 `dry_run=false`、阶段 1–8、ETA 和 `next_eta_check_at`。正式输出默认在 `~/nightly/<北京日期>-<RUN_ID>/`，预检输出默认在 `~/nightly-dry-run/`。实际目录以 `checks.jsonl` 的 `output_dir` 为准。记录真实 PID 和时区后再报告启动成功。

## 检查与收尾

- `checks.jsonl` 记录 ETA 到点检查、30 分钟巡检、顺延和诊断。阶段通过后 Dagu 自动执行证据核验，在同一目录写 `postcheck.json`。共同验收首先看 Dagu 最终 `state`，再看 `postcheck.json` 的 `issues` 和 `report.md`。原报告无法替代采样完整性检查。
- 验证进展和短作业示例看 [OpenSpec 验证记录](../../../openspec/changes/robot-dagu-nightly/verification.md)。业务进展以对应 worktree 的 `tasks.md`、交接和 `docs/baseline/nightly/` 的原始报告为准；有专项补测时按交接定位原始采样和退出标记。
- 自动续步运行 VM 内的验收步骤。读取证据、更新 Mac worktree 和按项目规则处理授权由接手对话完成。Dagu 复查写入作业记录，单次作业无 Codex 定时唤醒。
- 最后在对应 worktree 记录报告出处、有效证据和待办，收回本轮临时防睡。提交、推送和开 PR 按项目授权约定。 [Dagu 配置说明](../../../dagu/README.md) · [OpenSpec 验证记录](../../../openspec/changes/robot-dagu-nightly/verification.md)

### Mac 简报接续

新作业按 dagu/README.md 登记目标对话及变更。用户询问时用 status 读取结构化进度并直接回复，LaunchAgent 仅投递终态；归档通过项目 AGENTS.md 的完成步骤按分支清理已登记的 checks.jsonl。归档前可反复读取原证据。
