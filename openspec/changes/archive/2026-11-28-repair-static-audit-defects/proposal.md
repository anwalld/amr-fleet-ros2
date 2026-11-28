# Proposal

修复经蜂群独立源码复核确认的 `main` 静态审计真实缺陷（零外部接口破坏）。

## Why

2026-11-27 全项目静态蜂群审计报告（`docs/handoffs/changes/project-static-audit-2026-11-27.md`）提出 29 项缺陷与改进建议。经独立三路只读蜂群核查（复核报告见 `main` 的 `docs/handoffs/changes/project-static-audit-2026-11-27-review.md`），驳回了其中十余项误报或破坏性建议（如破坏 U18 幂等契约、移除 `ros_sweep` 或在子脚本重拿锁导致自死锁、把 `MissionRunnerNode` 改多线程、把 `console_ops_acceptance` 挂进 CTest、引入未安装 FastAPI/Pydantic、删掉合规 `via:/v2/submit_order` 等），同时确认了 15 项在 `main` 上真实存在且无需修改 ROS `.srv`/`.msg` 或 HTTP/CLI 外部契约即可安全落地的缺陷。

## What Changes

在独立分支 `ag/static-audit-fixes`（基线 `main@8766f99`）分三条互不重叠的文件轨道落地修复：

1. **核心 C++/ROS 安全修复（`src/`）**：
   - `P0-01` & `P1-06`：`src/robot_tasks/src/navigate_sequence_server_node.cpp` 落地 `worker_` 析构 `join`、`AttachLateCancelGuard`、`AbandonGoalWait`、暂停结算保护、`active_child_goal_token_` 防迟到反馈覆盖、原子化 `current_goal_index_`/`total_goals_` 与 `stats_mutex_`。
   - `P0-03`：`src/robot_tasks/include/robot_tasks/traffic_reservation.hpp`、`src/robot_tasks/src/traffic_reservation.cpp`、`src/robot_tasks/src/traffic_service.cpp`、`src/robot_tasks/test/traffic_reservation_test.cpp` 为 `PlanRouteSegmentRelease` 透传 `grant_options`（含 `frozen_robot_ids_` 与 `now_s`/`starvation_s`）与 `route_holds`。
   - `P1-05`：`src/robot_tasks/src/mission_runner_node.cpp` 保持单线程，为 `TryComputeNav2PathDistance` 增加总预算超时与 TTL 路径长度缓存。
   - `P1-07`（零接口改动部分）：`src/robot_tasks/src/fleet_runtime_state.cpp`、`src/robot_tasks/test/fleet_runtime_state_test.cpp` 增加 `FINISHED`/`CANCELED` 终态单调保护（同终态重复上报幂等返回 `true`）。
   - `P2-01`：`src/robot_console/CMakeLists.txt` 仅补注释标明 `console_ops_acceptance` 由阶段 7 编排驱动，不注册进 CTest。
2. **夜跑、Dagu、网关与桥接脚本安全修复（`scripts/`）**：
   - `P0-04`：`scripts/nightly.sh` 仅清理旧 `test_results`/`Testing`，分离捕获 `colcon test` 与 `colcon test-result` 退出码并断言测试总数 > 0。
   - `P0-05`：`scripts/nightly.sh`、`scripts/run_acceptance_baseline.sh`、`scripts/run_deadlock_scenarios.sh`、`scripts/run_console_smoke.sh` 落地双判据锁门禁（`! flock -n` + `kill -0 "$ROBOT_NIGHTLY_LOCK_OWNER"`）与 `${ROOT_DIR}` 收紧清场正则。
   - `P0-06` & `P2-10`：`scripts/dagu_nightly.py`、`scripts/dagu_nightly_evidence.py`、`scripts/dagu_brief_remote.py`、`scripts/dagu_chat.py` 补齐 `source.manifest` SHA-256 校验、异常/状态码 4 容错与 `killpg` 退出升级。
   - `P1-08`：`scripts/rest_api_gateway.py`、`scripts/external_callback_outbox.py` 纯标准库实现单飞幂等、请求体 SHA-256 409 冲突检测、5xx 不缓存、原子落盘与文件锁。
   - `P1-09` & `P1-11`：`scripts/vda5050_file_bridge.py`、`scripts/facility_plc_file_adapter.py`、`scripts/vda5050_mqtt_mock_bridge.py`、`scripts/vda5050_instant_action_bridge.py` 统一改用 `json.dumps` 构造 ROS 请求、补超时与路径穿越/symlink 防护，配套 `scripts/test_static_audit_fixes.py`。
3. **工程治理、CI/Docker 与文档修复（`tools/`、`.github/`、`.dockerignore`、`README.md`、`AGENTS.md`、`docs/`）**：
   - `P1-12`：`tools/codex_apply_helper.py`、`dagu/README.md` 明确范围防呆门禁定位，拦截 `..`/绝对路径/`.git/`/逃逸 symlink，并回填 `session_id`。
   - `P2-03`：`.github/workflows/ci.yml` 增加零依赖静态 lint 步骤与分离测试退出码；新增 `.dockerignore`。
   - `P2-08`：`tools/robot_guard.py` 对重名文件禁用 basename 回退；`scripts/lint_interface_coverage.py` 收紧 `via:` 为精确匹配 `via:/v2/submit_order` 且校验目标已覆盖。
   - `P2-06` / `P2-07` / `P2-09`：精简 `docs/handoffs/changes/worktree-consolidation.md`、删除 `AGENTS.md` 已废除的 Codex 30 分钟空额度巡检（保留运行期夜跑巡检）、补齐静态入口 `README.md`，配套 `tools/test_gov_audit_fixes.py`。

## Non-Goals

- 不修改任何 `.srv` / `.msg` 接口定义，不引入任何未安装的第三方 Python/系统依赖。
- 不修改受保护工作树 `verified-defect-fixes` 下的任何文件。
- 不在本轮离线窗口内执行 VM SSH 编译、测试或正式夜跑。
