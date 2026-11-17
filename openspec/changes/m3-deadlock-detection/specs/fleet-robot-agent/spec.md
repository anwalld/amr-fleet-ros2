## MODIFIED Requirements

### Requirement: 多车轻量仿真启动
系统 SHALL 提供启动文件 `fleet_sim.launch.py`，参数 `fleet_size`（默认 6）、`network`（`workshop`、`workshop_2way` 或 `warehouse`）、`traffic_reservation_mode`、`traffic_deadlock_resolution_enabled`（默认 `true`，透传给任务中枢），拉起任务中枢（车辆号为 `dispatcher`，不在车队里）、不依赖 Nav2 的导航序列服务和 `fleet_size` 个任务代理（`robot_1` … `robot_N`），各车起点即其停车位，按路网的停车位列表依次分配。车数超过该路网停车位数时 SHALL 启动失败。

#### Scenario: 6 台车启动
- **WHEN** 以 `fleet_size:=6 network:=workshop` 启动
- **THEN** 出现 `robot_1` … `robot_6` 六个代理，分别以 `park_1` … `park_6` 为起点和停车位

#### Scenario: 车数超过停车位
- **WHEN** 以 `fleet_size:=6 network:=warehouse` 启动（该路网只有 5 个起点）
- **THEN** 启动失败并说明该路网只有 5 个起点

#### Scenario: 双向路网启动
- **WHEN** 以 `fleet_size:=6 network:=workshop_2way` 启动
- **THEN** 六个代理在 `workshop_2way` 路网的停车位上就绪，任务中枢加载同一路网

### Requirement: 按段申请与释放路权
rolling 模式下代理 SHALL 用路网的最短路径逐段前进：为"下一条边 + 下一个路口"申请路权，申请时带上本程目的地 `goal_station`，授予后才驶向下一站；到达后释放身后的边和上一个路口，只继续持有当前路口。代理 SHALL 按授予应答（或授予通知）给出的资源号决定释放哪些资源，不自行拼接资源号。未授予时 SHALL 留在原地，收到 `/traffic/grants` 中给自己的通知立即重申请，否则每 2 秒重申请一次。

#### Scenario: 拿到路权才出发
- **WHEN** robot_2 下一段的路口被 robot_1 占用
- **THEN** robot_2 停在当前站点不动，直到收到授予

#### Scenario: 到站后释放身后资源
- **WHEN** robot_2 从 A 到达 B
- **THEN** robot_2 释放 A → B 这次授予给出的资源中除 `route_node:B` 外的全部（即该路段锁），以及上一次授予留下的 `route_node:A`，仍持有 `route_node:B`

#### Scenario: 等待期间改了目标
- **WHEN** 代理在等路权期间改去接新任务，之后旧申请被授予
- **THEN** 代理把这次授予的资源中除当前路口外的全部还回去

## ADDED Requirements

### Requirement: 执行让路指令
代理 SHALL 订阅 `/traffic/yield` 中发给自己的指令：`reroute` 时把指令中的资源加入临时绕行集合，按指令给出的新路径（或自行绕开这些资源重新寻路）继续本程；`backoff` 时先驶往指令中的避让点，在那里停留指令给出的时长，再从避让点重新寻路去原目的地；`pause` 时停在原地不再申请，直到自己持有的路权被运维清除。临时绕行集合 SHALL 在本程结束时清空。

#### Scenario: 收到改道指令
- **WHEN** robot_2 在 jn_s 等待时收到绕开 `route_node:machining` 的 `reroute` 指令
- **THEN** robot_2 改走不经过 machining 的路径并申请其第一段

#### Scenario: 收到退避指令
- **WHEN** robot_2 收到目标 park_3、停留 5 秒的 `backoff` 指令
- **THEN** robot_2 逐段申请驶往 park_3，到达后停留约 5 秒，再申请去原目的地的路径

#### Scenario: 收到暂停指令
- **WHEN** robot_2 收到 `pause` 指令
- **THEN** robot_2 停在当前站点，不再发出路权申请
