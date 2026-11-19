# Proposal

## Why

M2 的滚动路权能在单向环路上连续跑满 30 分钟，但 `/v2/detect_traffic_deadlock` 至今只报告"同一站点被两个以上占用者触及"的冲突候选：它在正常排队时也会误报，真正的循环等待却找不到，更不会去解开。M2 的压测已经说明问题是真实的：whole_route 在 2～4 分钟内互等停摆；车间路网之所以没在 rolling 下出现死锁，只是因为它全是单向边、最短环比车数长。路网一旦有双向段或短环，rolling 也会循环等待。M3 要把死锁检测改成等待图找环，并用改道或退避自动解开，用同一套压测给出 rolling 与 whole_route 的对照数据。

## What Changes

- **死锁检测改为等待图找环**：节点是车，"车 A 等的资源被车 B 占着"即 A → B；图中有环即死锁。每次有申请进入等待时检测一次，另每秒全量检测一次。`/v2/detect_traffic_deadlock` 的服务名和类型不变，但语义改为报告当前的等待环（**BREAKING**：语义变化，旧的"站点冲突候选"不再报告；字段沿用，`mission_ids` 里放的是环上的车辆号）。
- **死锁解除**：在环上选一辆让路车（优先级最低，同优先级时剩余路程最长）；能绕开环上被占资源到达目的地就改道，否则退到最近的空闲避让点，让对方先过；同一组车 30 秒内第三次成环则报警并暂停这几台车，等人工处理。解除可用参数关闭（只检测不解除）。
- **先修 M2 补规格时记下的三处隐患**（见 `openspec/changes/archive/2026-11-17-backfill-m0-m2-specs/design.md`）：
  - 占用者统一为车辆号：whole_route 下任务中枢自驱的站点运输也改用车辆号占用（**BREAKING**：`/traffic/state`、占用查询里不再出现任务号占用者）。
  - 路段锁命名只保留核心代码一份：车队代理按申请应答给出的资源号释放，压测脚本解析交通状态里的资源号，不再各自拼名字。
  - 拿不齐整组路段的高优先级等待者等待超过阈值后，它需要的已空闲资源暂扣给它，不再一直被低优先级车插队。
- **接口新增**（名字和类型都是新的，或在原消息上追加字段）：
  - `/v2/request_route_segment` 请求追加可选字段 `goal_station`（本程目的地，供改道和剩余路程计算；为空时该车只能被退避）。
  - `/traffic/state` 追加死锁统计字段：检测次数、检出次数、当前环、最近一次解除动作与耗时、单次检测耗时。
  - 新话题 `/traffic/yield`（新消息 `TrafficYield`）：发给让路车的改道 / 退避 / 暂停指令。
- 路网配置支持标记避让点；新增一张含双向段和短环的车间路网 `workshop_2way`，供压测与死锁场景使用；原 `workshop` 保持不变。
- 车队代理执行让路指令。
- 压测：`scripts/run_fleet_traffic_stress.sh`（及其调用的 `fleet_traffic_stress.py`）直接加死锁指标列：检出次数、解除方式与耗时、单次检测耗时；不另写压测脚本。新增夜跑阶段 6 的死锁场景脚本（两车对向、三车成环、环外有车等待）。
- 对照实验即夜跑阶段 5 的矩阵：2 / 4 / 6 台 × whole_route / rolling。

## Capabilities

### New Capabilities

（无）

### Modified Capabilities

- `route-reservation`：死锁检测从站点冲突候选改为等待图找环，新增解除（改道、退避、兜底暂停）与开关；占用者统一为车辆号；whole_route 被拒的申请计入等待图；补授予增加防饿死；申请服务追加 `goal_station`；交通状态追加死锁字段；新增让路指令话题。
- `fleet-robot-agent`：按授予应答里的资源号释放路权（不再自己拼名字）；执行让路指令；轻量仿真支持 `workshop_2way` 路网。
- `acceptance-validation`：压测输出增加死锁指标、可选路网；新增死锁场景脚本；交通死锁验收用例改按等待环语义判定。

## Impact

- 代码：`robot_tasks`（`traffic_reservation.*` 新增等待图、选让路方、改道 / 退避规划等纯函数及 GoogleTest；`traffic_service.*` 接线；任务中枢 whole_route 占用者改车辆号），`robot_simulation/fleet_robot_agent_node.cpp`，`robot_bringup/launch/fleet_sim.launch.py`。
- 接口：`robot_interfaces_mission` 的 `RequestRouteSegment.srv`（追加字段）、`TrafficState.msg`（追加字段）、新 `TrafficYield.msg`。`DetectTrafficDeadlock.srv` 不改。追加字段后旧的录包不能直接回放，仓库里没有依赖旧录包的东西。
- 配置：`robot_tasks/config/stations/` 下站点可标 `yield_point`，新增 `workshop_2way_stations.yaml`。
- 脚本：`scripts/run_fleet_traffic_stress.sh`、`scripts/fleet_traffic_stress.py`、`scripts/check_traffic_deadlock_detection.sh`，新增阶段 6 场景脚本；`scripts/nightly.sh` 的阶段 5、6 从 SKIP 转为实跑。
- 验证：白天只跑 `colcon test --packages-select robot_tasks`（及改动到的包）；全栈验证、对照数据、场景结果全部来自夜跑报告。
- 不在范围内：独立的交通管制节点（M2 已定 `TrafficService` 留在任务中枢进程内）；任务中枢自驱的站点运输执行改道 / 退避（它被选为让路车时只撤等待、稍后重申请，见 design）；Gazebo 演示场景。
