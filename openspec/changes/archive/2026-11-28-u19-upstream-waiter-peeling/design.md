# Design

## Context

拓扑解剖、三次停摆现场与 8,008 布局穷举见 `explore-u19-zero-escape-avoidance/design.md`。本变更只落地其中的方案 C 与 U19-3。实施基线为四方合流后的 `codex/main-merge-candidate`，其中 `traffic_deadlock.{hpp,cpp}` 已包含 `rolling-backoff-*` 的 `PlanYieldWithAvailability`、`require_clear_path` 与 `home_owners`。

## Goals / Non-Goals

**Goals**
- 等待分量中只要存在一辆能退的车，就不触发 `pause`。
- 对外 ROS 接口、`/traffic/yield` 消息格式、代理 `backoff` / `reroute` 状态机保持不变。
- 阶段 6 九场景与现有 `traffic_deadlock_test` 用例行为不变（环内可解时不进入第二阶段）。

**Non-Goals**
- 不做方案 A/B 的事前准入或割集前瞻。
- 不处理 Gazebo 主车让路（U19-2）。
- 不放宽阶段 5 停摆判据，不移除兜底 `pause`。

## Decisions

### D1 两阶段解除

1. 第一阶段沿用现有逻辑：在环 `C` 内按优先级选车 `reroute` / `backoff`。
2. 环内全部车的候选都是 `pause` 时进入第二阶段：
   - 计算等待分量 `W = C ∪ U(C)`，`U(C)` 为沿等待边指向 `C` 的环外上游等待车（按跳数分层，BFS 向外扩展）。
   - 按层由近到远，同层按现有让路优先级与剩余路程排序，对每辆 `u` 调用 `PlanYieldWithAvailability`，阻塞集合为 `W \ {u}` 的占用节点，`require_clear_path = true`，沿用 `home_owners` 偏好。
   - 选第一辆得到 `reroute` 或 `backoff` 的车 `u*`，产出 `DeadlockResolution{action, yield_robot = u*, peeling = true}`。
   - 所有 `u` 均无解时返回 `pause`（行为与现状一致）。
3. 纯函数实现，放在 `traffic_deadlock.cpp`，便于离线穷举单测。

### D2 剥离期状态

- `ActiveDeadlock` 新增 `peeling_robot_id`（空表示非剥离）与 `peeling_started_s`。
- 剥离期间，同一环再次被检测到时不累加 `deadlock_history_`（30 秒 3 次升级计数）。
- `u*` 释放任一属于 `ext_out(C)` 的节点后，立即对原环重算一次解除。
- `u*` 超过 `yield_timeout_s` 未动，清除剥离标记并按原规则累加计数，可能落到 `pause`。

### D3 U19-3

`FleetTaskAgent::Route()` 重试循环每轮开头检查 `paused_` 与 `pending_yield_`：`paused_` 为真时退出重试并按暂停路径停车；`pending_yield_` 有值时放弃当前申请并转入让路处理。释放与上报继续走 `lease_tracker_` / `Release` / `UpdateFleetState`，不绕过 U18 租约。

## Risks / Trade-offs

- **剥离期误升级为 pause** → D2 冻结计数，并以释放出口事件触发重算。
- **两车退避路线互撞（`rolling-diagnostic` 1669s→1680s）** → 第二阶段把 `W \ {u}` 全部视为阻塞且要求整条路线畅通。
- **多跳剥离增加局部等待** → 只在环内零退路时触发；压测记录 `peeling` 次数，若吞吐低于 120 单/小时再评估方案 B。

## Verification Plan

1. `traffic_deadlock_test`：新增 `workshop_2way` 8,008 布局穷举，断言「存在可退车 ⇒ 不返回 pause」；新增 `v4-formal` 与 `rolling-diagnostic` 两个现场复刻用例。
2. 现有 `traffic_deadlock_test`、`traffic_reservation_test`、`agent_lease_test` 全绿。
3. VM 隔离域：`run_deadlock_scenarios.sh` 9/9；6 车 `rolling` 30 分钟压测 0 违规、0 `pause` 停摆。
