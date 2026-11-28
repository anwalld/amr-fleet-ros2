# Proposal

U19 第一阶段：环外上游等待车剥离退避（方案 C）+ 代理 `Route()` 让路消费（U19-3）

## Why

六车 `rolling` 压测在 `workshop_2way` 上多次复现「零退路」停摆：环内两车对向等待，环的全部外部出站口被环外上游等待车占满，`PlanDeadlockResolution` 对环内每辆车都找不到改道或空闲避让点，只能触发兜底 `pause`（M3 首轮 963s、第二遍 1659s、`v4-formal` 1606.65s、`rolling-diagnostic` 1680.52s）。探索变更 `explore-u19-zero-escape-avoidance` 离线穷举 `workshop_2way` 全部 8,008 种 6 车占站布局，证明候选 C 的无解率为 `0/8,008`。

用户已于 2026-11-28 拍板：
1. 采纳方案 C（环外上游等待车剥离退避）+ U19-3，暂不叠加方案 B/D；
2. U19-2（Gazebo 主车完整改道/退避）继续按需单独立项，不纳入本变更；
3. 本变更纳入 `codex/main-merge-candidate` 同分支收口，在四方合流底座全绿后实施。

## What Changes

- **U19-1（方案 C）**：`PlanDeadlockResolution` 保留第一阶段环内选车。环内所有车都只能 `pause` 时，进入第二阶段：从堵住环外部出站口的环外上游等待车中，按既有优先级与剩余路程规则，逐层向外选出第一辆能绕开等待分量到达空闲避让点（优先公共避让点或本车停车位，路径须全程畅通）或改道的车，向它发 `backoff` / `reroute`。对外接口零变更，仍经 `/traffic/yield` 下发。
- **剥离期状态**：`ActiveDeadlock` 记录 `peeling_robot_id`。剥离期间不累加原环的「30 秒内 3 次成环」暂停计数；剥离车释放邻接节点后立即重算原环。剥离车超时未动、或整个等待分量确实无车可退时，仍回落到既有安全 `pause`。
- **U19-3**：`FleetTaskAgent::Route()` 重试循环入口消费 `paused_` 与 `pending_yield_`，代理停在 `Route()` 重试期间收到的暂停或让路指令不再被吞掉。

## Capabilities

### Modified Capabilities

- `route-reservation`：新增「零退路时剥离环外上游等待车」要求。
- `fleet-robot-agent`：新增「路段申请重试期间响应暂停与让路」要求。

## Impact

- 代码：`src/robot_tasks/{include/robot_tasks/traffic_deadlock.hpp,src/traffic_deadlock.cpp,src/traffic_service.cpp}`、`src/robot_simulation/src/fleet_robot_agent_node.cpp` 及对应单测；不改 `.srv` / `.msg`。
- 依赖：基线为合入 `rolling-backoff-home-preference` 与 `rolling-backoff-route-availability` 后的 `codex/main-merge-candidate`，复用其 `require_clear_path` 与 `home_owners`。
- 验证：`traffic_deadlock_test` 新增 8,008 布局穷举回归、阶段 6 九场景不回归、VM 6 车 `rolling` 30 分钟隔离压测零 `pause` 停摆；正式共同夜跑须用户另行授权。
