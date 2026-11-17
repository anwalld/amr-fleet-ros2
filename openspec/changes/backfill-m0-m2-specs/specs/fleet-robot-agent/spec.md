# Spec Delta

## Purpose

定义多车轻量仿真中每台车的代理行为：在不启动 Gazebo 的前提下，领取派给自己的车队任务，按路权规则逐段申请、行驶、释放，并把任务进度和统计回报给任务中枢，使 N 台车可以同时在一张路网上运行和压测。

## ADDED Requirements

### Requirement: 多车轻量仿真启动
系统 SHALL 提供启动文件 `fleet_sim.launch.py`，参数 `fleet_size`（默认 6）、`network`（`workshop` 或 `warehouse`）、`traffic_reservation_mode`，拉起任务中枢（车辆号为 `dispatcher`，不在车队里）、不依赖 Nav2 的导航序列服务和 `fleet_size` 个任务代理（`robot_1` … `robot_N`），各车起点即其停车位，按路网的停车位列表依次分配。车数超过该路网停车位数时 SHALL 启动失败。

#### Scenario: 6 台车启动
- **WHEN** 以 `fleet_size:=6 network:=workshop` 启动
- **THEN** 出现 `robot_1` … `robot_6` 六个代理，分别以 `park_1` … `park_6` 为起点和停车位

#### Scenario: 车数超过停车位
- **WHEN** 以 `fleet_size:=6 network:=warehouse` 启动（该路网只有 5 个起点）
- **THEN** 启动失败并说明该路网只有 5 个起点

### Requirement: 起点占位
代理启动后 SHALL 先申请自己起点站的路口锁；拿到后才在车队里登记为 `available` 并开始领任务。拿不到时每秒重试。

#### Scenario: 起点路口被占
- **WHEN** 代理的起点路口已被其他车占用
- **THEN** 代理不登记为 available，持续重试直到拿到该路口锁

### Requirement: 领取车队任务
空闲的代理 SHALL 每 0.5 秒查询派给自己的、状态为 `DISPATCHED_REMOTE` 的车队任务，领取第一个，把任务状态更新为 `RUNNING` 并把自己登记为 `busy`。正在回停车位、且停在站点上（还在寻路或等路权、没有在途的申请）的代理也 SHALL 能改去接新任务；已经驶离站点的不改。

#### Scenario: 领取任务
- **WHEN** 任务中枢把一个车队任务派给 robot_3，robot_3 空闲
- **THEN** robot_3 领取该任务，任务状态变为 `RUNNING`，消息含 `accepted by robot_3`

### Requirement: 按段申请与释放路权
rolling 模式下代理 SHALL 用路网的最短路径逐段前进：为"下一条边 + 下一个路口"申请路权，授予后才驶向下一站；到达后释放身后的边和上一个路口，只继续持有当前路口。未授予时 SHALL 留在原地，收到 `/traffic/grants` 中给自己的通知立即重申请，否则每 2 秒重申请一次。

#### Scenario: 拿到路权才出发
- **WHEN** robot_2 下一段的路口被 robot_1 占用
- **THEN** robot_2 停在当前站点不动，直到收到授予

#### Scenario: 到站后释放身后资源
- **WHEN** robot_2 从 A 到达 B
- **THEN** robot_2 释放 `route_edge:A__B`（按字典序命名）和 `route_node:A`，仍持有 `route_node:B`

#### Scenario: 等待期间改了目标
- **WHEN** 代理在等路权期间改去接新任务，之后旧申请被授予
- **THEN** 代理把这次授予的资源中除当前路口外的全部还回去

### Requirement: 封路时改道
代理 SHALL 依据 `/traffic/state` 中的封路标记维护被封路段集合并在寻路时绕开；申请应答 `replan_required` 或收到 `replan` 通知时 SHALL 重新寻路。没有可达路径时留在原地并限频告警。

#### Scenario: 申请撞上封路
- **WHEN** 代理申请的路段被封
- **THEN** 代理绕开该路段重新寻路后继续申请

### Requirement: whole_route 对照模式
`traffic_reservation_mode:=whole_route` 时代理 SHALL 一次申请从当前站点到本程终点的整条路径；拿不到时隔 2 秒加 0～1 秒随机抖动后重试；拿到后连续驶完整条路径，到终点才释放除当前路口外的全部资源。

#### Scenario: 拿不到整条路径时重试
- **WHEN** whole_route 模式下代理的整条路径中有一处被占
- **THEN** 代理不出发，2～3 秒后重新申请整条路径

### Requirement: 装卸停留与任务完成回报
代理到达取货站和卸货站后 SHALL 各停留 `dwell_s`（默认 3 秒），取货后把任务状态更新为 `RUNNING`（消息 `loaded at <站点>`），卸货后更新为 `FINISHED`（消息含 `duration_s`、`wait_route_s`、`distance_m`），并在 `/fleet/agent_events` 发布一条 JSON：车辆、任务、取货站、卸货站、模式、开始与结束时间、耗时、等路权时间、里程。之后登记为 `available`。

#### Scenario: 任务完成事件
- **WHEN** robot_1 卸货停留结束
- **THEN** 任务状态为 `FINISHED`，`/fleet/agent_events` 收到一条 `"robot":"robot_1"` 的 JSON，含 `wait_route_s` 与 `distance_m`

### Requirement: 空闲回停车位
`return_home_when_idle` 为真（默认）时，没有任务且不在自己停车位的代理 SHALL 按同样的路权规则驶回停车位。

#### Scenario: 卸货后回停车位
- **WHEN** robot_4 完成任务后没有新任务，且当前不在 `park_4`
- **THEN** robot_4 逐段申请路权驶回 `park_4`

### Requirement: 运动学仿真
`kinematic_sim:=true` 时代理 SHALL 不依赖 Gazebo：按自己算出的速度指令积分位置（最高线速度 `max_linear_mps`，默认 0.8 m/s），以 `<命名空间>/odom` 发布里程计（坐标系 `map`）。

#### Scenario: 发布里程计
- **WHEN** robot_2 以运动学模式运行
- **THEN** `/robot_2/odom` 持续发布，位置随行驶变化，速度不超过 0.8 m/s

### Requirement: Gazebo 两车演示兼容
代理 SHALL 保留 `mode:=follow_fleet_station`（默认）：按车队状态里的当前站点点到点行驶，不申请路权，供 Gazebo 两车演示场景使用，行为与原 robot_2 演示驱动一致。

#### Scenario: 演示模式不申请路权
- **WHEN** 代理以 `mode:=follow_fleet_station` 运行
- **THEN** 它不调用路权申请服务，只跟随车队状态中的站点行驶
