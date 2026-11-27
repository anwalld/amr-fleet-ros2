# 收敛施工单（Apply Brief）标准模板

> 本模板严格匹配 `/Users/mima0000/CLionProjects/robot/tools/codex_apply_helper.py` 的 `REQUIRED_SECTIONS` 与正则机检。使用本模板生成 `$RUN_DIR/apply-brief.md` 时，**严禁增删或调换以下 7 个二级标题顺序**。

---

## 1. 执行环境与硬边界

- worktree: `/Users/mima0000/CLionProjects/robot-worktrees/<worktree-dir>`
- branch: `<branch-name>`
- HEAD: `<40-char-full-commit-sha>`
- change: `<openspec-change-name>`

## 2. 允许修改文件白名单

- `<relative/path/to/file1>`
- `<relative/path/to/file2>`

## 3. 现状锚点与精确修改契约

### `<relative/path/to/file1>`
- **现状锚点**：`L<start>-L<end>`，现有符号 `<Class::Method / function>`，当前行为与缺陷根因。
- **精确修改要求**：
  1. 数据结构/字段/签名变更（若无接口变更显式说明）。
  2. 确定性状态转移与边界条件处理。
  3. 跨分支/主线交汇保护点（需完整保留的现有逻辑）。

## 4. 显式禁止项

- 严禁修改白名单之外的任何文件。
- 严禁在 `robot_interfaces_*` 中增删接口文件名（保持 `interface_coverage.tsv` 行数不变）。
- 严禁删除或放宽既有测试断言。
- 严禁在 VM 隔离验证期间调用全局 `ros_sweep` 或 `pkill`。
- 严禁执行 `git commit`、`git push`、`git reset`、`git checkout`。
- 严禁勾选 `tasks.md` 末项「正式共同夜跑验收」。

## 5. 固定验证配方

- **VM 主机**：`ubuntu@172.16.255.133`
- **隔离工作区**：`~/robot-dagu-workspaces/<isolated-dir>`
- **固定隔离域**：`ROS_DOMAIN_ID=<200-239>`
- **编译命令**（若改动接口包，必须先编接口包再 `source install/setup.bash` 编下游）：
  ```bash
  colcon build --packages-select <pkg1> <pkg2>
  ```
- **定向测试与探针命令**：
  ```bash
  <exact test / probe command>
  ```
- **通过判据关键字**：`<expected pass output / 0 errors, 0 failures>`

## 6. 异常停机准则

满足以下任一条件必须立即停手，严禁自行发散绕过，在回执中填 `status: "blocked"` 并附原始报错：
1. 源码现状与第 3 节锚点不符；
2. 必须修改第 2 节白名单之外的文件才能完成；
3. 同一编译或测试报错连续修补 2 次仍未通过；
4. 检测到 VM `~/.robot-nightly.lock` 已被正式夜跑占用。

## 7. 结构化回执

输出 JSON 严格遵循 `$RUN_DIR/apply-receipt.schema.json`，包含字段：`status`（`completed | blocked | failed`）、`modified_files`、`verification_runs`（`command`, `exit_code`, `log_path`, `key_evidence`）、`invariants_checked`、`blocked_reason`、`notes_for_reviewer`。
