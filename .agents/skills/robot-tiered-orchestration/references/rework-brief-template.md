# 单次返工单（Rework Brief）标准模板

> 仅在 `neon-check` 第 1 次判定 `REWORK` 且缺陷账本 `escalate=false` 时使用；通过 `dagu/codex-apply.yaml` 的 `RESUME_SESSION_ID` 与 `REWORK_BRIEF_FILE` 续接原 Codex 会话。**最多允许返工 1 次**，若返工后仍未通过必须立即回升 `Carbon` 主会话重设计。

---

## 1. 返工上下文

- 原派单运行目录：`<$RUN_DIR>`
- 续接 `session_id`：`<from return-bundle.json>`
- 允许修改白名单：**严格沿用原 `apply-brief.md` 第 2 节白名单，不得新增任何路径**。

## 2. `neon-check` 审计未通过项（逐条闭环）

1. **问题 1**（文件与行号：`<path:line>`）
   - **原始证据**：`<diff 片段或失败日志原文>`
   - **精确修正要求**：`<最小必要修正，不发散改设计>`

## 3. 复验命令与通过判据

```bash
<exact re-verification command>
```
- 通过判据：`<exact expected output>`

## 4. 停机与回执要求

- 若修正需要触碰原白名单之外的文件，或同一问题再次失败，立即停手并在 `apply-receipt.json` 中返回 `status: "blocked"`。
- 完成后按同一 `apply-receipt.schema.json` 输出更新后的结构化回执。
