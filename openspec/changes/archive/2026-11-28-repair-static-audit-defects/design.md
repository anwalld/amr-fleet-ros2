# Design

本变更严格限定为经蜂群复核确认的内部缺陷修复与工程门禁加固，保持全部 ROS 接口（`.srv`/`.msg`/话题/服务名）、REST/Outbox/CLI 外部行为契约与既有单测 100% 向后兼容。

## 关键设计决策

1. **C++ 线程与状态安全（`P0-01`、`P0-03`、`P1-05`、`P1-06`、`P1-07`）**：
   - `NavigateSequenceServerNode` 改 `detach()` 为受控 `worker_` 成员并在析构前安全 `join()`；用 `AttachLateCancelGuard` 与 `AbandonGoalWait` 防止取消/超时后迟到子目标执行；用单调递增 `active_child_goal_token_` 隔离迟到 `FollowWaypoints` 反馈；`current_goal_index_`/`total_goals_` 改 `std::atomic<int>`，`statistics_` 由 `stats_mutex_` 保护。
   - `PlanRouteSegmentRelease` 增加带默认值的 `grant_options` 与 `holds` 参数，既有单测零改动兼容；`TrafficService::ReleaseRouteSegment` 透传 `{NowSeconds(), wait_starvation_s_, &frozen_robot_ids_}` 与 `&context_.route_holds`。
   - `MissionRunnerNode` 严禁改多线程；`TryComputeNav2PathDistance` 增加总预算超时（超时取消子目标并回退路网/欧氏距离）与 `(frame, planner, 量化起终点)` TTL 缓存。
   - `PlanRemoteFleetTaskStateUpdate` 对 `FINISHED` / `CANCELED` 终态实施单调保护，拒绝向非终态倒退，同终态重复上报幂等返回 `true`。
2. **脚本锁门禁与标准库加固（`P0-04`、`P0-05`、`P0-06`、`P1-08`、`P1-09`、`P1-11`、`P2-10`）**：
   - 保持 `nightly.sh` 通过 `9>&-` 不向子进程泄漏 fd 9，改为写锁持有者 PID 并导出 `ROBOT_NIGHTLY_LOCK_OWNER=$$`，子脚本以 `! flock -n` + `kill -0 "$ROBOT_NIGHTLY_LOCK_OWNER"` 双判据验真，避免自死锁。
   - REST/Outbox/桥接脚本全部采用 Python 标准库（`threading.Condition`、`fcntl.flock`、`os.fsync` + `os.replace`、`json.dumps`），不引入 `FastAPI`/`Pydantic`。
   - `source.manifest` 校验完全基于工作区本地文件 SHA-256，不依赖 VM tar 包中不存在的 `.git`。
3. **治理与 CI 门禁（`P1-12`、`P2-03`、`P2-06`、`P2-07`、`P2-08`、`P2-09`）**：
   - `codex_apply_helper.py` 定位为受信任代理的范围防呆与 diff 审计门禁，拒绝路径穿越与逃逸 symlink，回填 `session_id`。
   - `robot_guard.py` 仅对全仓唯一文件名允许 basename 回退，重名文件强制相对路径边界匹配；`lint_interface_coverage.py` 收紧 `via:` 为 `via:/v2/submit_order` 并校验目标覆盖。
   - `ci.yml` 增加零依赖静态 lint 步并分别检查 `colcon test` 与 `colcon test-result` 退出码；新增根 `.dockerignore` 保留开发挂载兼容。
