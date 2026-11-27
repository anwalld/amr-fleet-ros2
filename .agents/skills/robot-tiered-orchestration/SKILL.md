---
name: robot-tiered-orchestration
description: >-
  Orchestrate robot repository development in a single Antigravity Carbon main session
  using the Carbon > Neon > Hydrogen hierarchy and Mac Dagu (dagu/codex-apply.yaml)
  dispatch to Codex GPT-6.2 Sol. Use when implementing OpenSpec tasks, fixing C++/ROS
  or script defects, or running multi-step code changes with independent read-only verification.
---

# robot 跨工具分层编排工作流（`Carbon > Neon > Hydrogen` + Mac Dagu）

本 Skill 用于在**单个 Antigravity 主会话（`Carbon`）**内统一编排 robot 仓库的探索、施工与独立审计，避免多顶层窗口并发撞车。

---

## 一、Step 0：前置环境与守卫检查

启动任何代码改动前，主会话必须核对：

1. **Git 与 Worktree 状态**：
   - 确认目标 worktree 绝对路径（位于 `/Users/mima0000/CLionProjects/robot-worktrees/<name>`）、当前分支名与 40 位完整 `HEAD` 哈希。
   - 确认目标 OpenSpec 变更 `openspec/changes/<change>/tasks.md` 存在（若为新设计或实质性设计变更，必须先走 `/opsx:explore` 与用户确认并完成设计 `/opsx:verify`）。
2. **缺陷账本熔断预检**：
   ```bash
   python3 -B /Users/mima0000/CLionProjects/robot/tools/robot_guard.py check-defects \
     "$WORKTREE/docs/handoffs/${CHANGE}-defects.tsv"
   ```
   - 若返回 `escalate=true`，**严禁进入施工**，立即停在 `Carbon` 主会话做只读根因诊断与重设计。
3. **VM 夜跑锁互斥检查**（仅当本次任务需要 SSH 到 VM 编译/测试时）：
   - 确认 VM（`ubuntu@172.16.255.133`）上 `~/.robot-nightly.lock` 未被正式夜跑占用；若正式夜跑正在运行，禁止派发任何触碰 VM 的施工单。

---

## 二、Step 1：任务分级路由

| 任务类型 | 判定条件 | 执行路径 |
| --- | --- | --- |
| **设计 / 决策类** | `/opsx:explore`、架构权衡、根因证伪、OpenSpec 文档定稿、熔断重设计、经用户授权的 `git commit/push/PR` | 留在 **`Carbon` 主会话**直接处理 |
| **轻量本地施工类** | ≤ 3 个非 C++ 文件（文档整理、YAML/JSON 配置、独立轻量 Shell/Python 小修），不涉及 C++ 编译或 ROS 接口改动 | 派生 **`hydrogen`** 子代理执行 → 交 **`neon-check`** 独立审计 |
| **重型编码施工类** | 任何 C++/ROS 节点代码、`.srv`/`.msg` 接口、多文件联动修改、或需 VM `colcon build` 与隔离域探针验证 | 派生 **`neon-spec`** 编译 7 段式施工单 → **Mac Dagu (`dagu/codex-apply.yaml`)** 派给 **Codex `GPT-6.2 Sol`** → 交 **`neon-check`** 独立审计 |

> **主会话自约束**：激活本 Skill 后，`Carbon` 主会话不直接编写大段 C++/ROS 业务代码，也不得跳过 `neon-check` 直接勾选 `tasks.md`。

---

## 三、Step 2：派生子代理（按需注册并复用）

详细 `define_subagent` 参数与完整系统提示词见 [references/subagent-prompts.md](./references/subagent-prompts.md)。首次需要某角色时读取该文件并注册：

- **`neon-spec`**（`Model: "flash"`, `EnableWriteTools: false`）：严格只读源码，读取真实文件路径、行号与符号签名，将 Carbon 设计编译为严格符合 [references/apply-brief-template.md](./references/apply-brief-template.md) 的 7 段式 `apply-brief.md`。
- **`neon-check`**（`Model: "flash"`, `EnableWriteTools: false`）：严格只读审计 `return-bundle.json`、`apply-receipt.json`、`git diff`、原始测试日志与 `robot_guard.py verify-change`，输出三态结论 `PASS | REWORK | ESCALATE`。
- **`hydrogen`**（`Model: "flash_lite"`, `EnableWriteTools: true`）：执行轻量本地白名单修改与自检，严禁执行 `git commit/push/reset/checkout`。

---

## 四、Step 3：Mac Dagu 跨工具派单（重型施工路径）

### 4.1 创建 Worktree 外部运行目录
运行目录必须落在目标 `WORKTREE` 之外（防止污染 `git status` 触发白名单失败）：

```bash
RUN_ID="$(date +%Y%m%d-%H%M%S)-${CHANGE}"
RUN_DIR="<conversation-artifact-dir>/scratch/codex-apply/${RUN_ID}"
mkdir -p "$RUN_DIR/control/dagu"
```

将 `neon-spec` 生成的 7 段式施工单写入 `"$RUN_DIR/apply-brief.md"`。

### 4.2 启动 `dagu/codex-apply.yaml`
使用 Mac 本机 `~/.local/bin/dagu` 与主检出权威配置 `/Users/mima0000/CLionProjects/robot/dagu/codex-apply.yaml`：

```bash
~/.local/bin/dagu start \
  --dagu-home "$RUN_DIR/control/dagu" \
  /Users/mima0000/CLionProjects/robot/dagu/codex-apply.yaml -- \
  "WORKTREE=$WORKTREE" \
  "CHANGE=$CHANGE" \
  "BRIEF_FILE=$RUN_DIR/apply-brief.md" \
  "RUN_DIR=$RUN_DIR" \
  "EXPECTED_HEAD=$EXPECTED_HEAD"
```

- 该命令依次执行 `preflight`（`codex_apply_helper.py preflight` 校验 7 段式格式/HEAD/账本）→ `apply`（`codex exec -m gpt-6.2-sol -c model_reasoning_effort="high"`）→ `collect`（白名单 diff 机检并输出 `$RUN_DIR/return-bundle.json`）。
- 若 `apply-receipt.json` 中 `status == "blocked"`：**0 等待立即回升 `Carbon` 主会话**分析阻塞原因，不走返工。

---

## 五、Step 4 & Step 5：独立审计、返工上限与熔断落盘

1. **强制后置审计**：无论 `Codex Sol` 还是 `Hydrogen` 完成施工，主会话立即调用 `neon-check` 独立核验。
2. **三态结果处理**：
   - **`PASS`**：
     - 更新 `docs/handoffs/<change>-defects.tsv`（记 `fixed`）并运行 `robot_guard.py check-defects`；
     - 更新 `openspec/changes/<change>/verification.md` 与 `tasks.md`（**严禁勾选末项「正式共同夜跑验收」**，除非已取得最新主线冻结快照的匹配 `source.manifest` 与零问题 `postcheck.json`）；
     - 汇报改动摘要与实测证据，等待用户授权 Git 提交或下一步指令。
   - **`REWORK`（最多允许 1 次）**：
     - 更新缺陷账本并检查 `escalate`；若 `escalate=true` 或已是第 2 轮未过，**立即转入 `ESCALATE`**；
     - 若为 `Codex Sol` 第 1 次返工：从 `$RUN_DIR/return-bundle.json` 提取 `session_id`，按 [references/rework-brief-template.md](./references/rework-brief-template.md) 写入 `$RUN_DIR/rework-brief.md`，传入 `RESUME_SESSION_ID` 与 `REWORK_BRIEF_FILE` 重跑 `dagu/codex-apply.yaml`，随后再次交 `neon-check`。
     - 若为 `Hydrogen` 第 1 次返工：向原 `hydrogen` 子代理发送 `send_message` 精确返工要求，随后再次交 `neon-check`。
   - **`ESCALATE`（`blocked` / 第 2 次 `REWORK` / `escalate=true`）**：
     - 立即停止一切施工派单，将账本与失败证据落盘，回到 `Carbon` 主会话仅做只读根因诊断与重新设计，按 `/opsx:explore` 与 `/opsx:verify` 确认新方案后再重新派单。
