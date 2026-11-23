# fleet-robot-agent Specification

## Purpose
定义多车轻量仿真中每台车的代理行为：在不启动 Gazebo 的前提下，领取派给自己的车队任务，按路权规则逐段申请、行驶、释放，并把任务进度和统计回报给任务中枢，使 N 台车可以同时在一张路网上运行和压测。

## Requirements

### Requirement: 多车轻量仿真启动
系统 SHALL 提供启动文件 `fleet_sim.launch.py`，参数 `fleet_size`（默认 6）、`network`（`workshop`、`workshop_2way` 或 `warehouse`）、`traffic_reservation_mode`、`traffic_deadlock_resolution_enabled`（默认 `true`，透传给任务中枢）、`start_stations`（逗号分隔，覆盖各车起点）、`home_stations`（逗号分隔，覆盖各车停车位；不给时停车位即起点），拉起任务中枢（车辆号为 `dispatcher`，不在车队里）、不依赖 Nav2 的导航序列服务和 `fleet_size` 个任务代理（`robot_1` … `robot_N`），未给 `start_stations` 与 `home_stations` 时，各车起点即其停车位，按路网的停车位列表依次分配。车数超过该路网停车位数时 SHALL 启动失败。

#### Scenario: 6 台车启动
- **WHEN** 以 `fleet_size:=6 network:=workshop` 启动
- **THEN** 出现 `robot_1` … `robot_6` 六个代理，分别以 `park_1` … `park_6` 为起点和停车位

#### Scenario: 车数超过停车位
- **WHEN** 以 `fleet_size:=6 network:=warehouse` 启动（该路网只有 5 个起点）
- **THEN** 启动失败并说明该路网只有 5 个起点

#### Scenario: 起点与停车位分开
- **WHEN** 以 `fleet_size:=3 start_stations:=jn_s,machining,jn_n home_stations:=park_1,park_2,park_3` 启动
- **THEN** 三台车分别在 jn_s、machining、jn_n 就绪，办完单空闲时分别回 park_1、park_2、park_3

#### Scenario: 双向路网启动
- **WHEN** 以 `fleet_size:=6 network:=workshop_2way` 启动
- **THEN** 六个代理在 `workshop_2way` 路网的停车位上就绪，任务中枢加载同一路网

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
`return_home_when_idle` 为真（默认）时，没有任务且不在自己停车位的代理 SHALL 按同样的路权规则驶回停车位，但仅限该代理自启动以来至少完成过一个任务；刚启动、尚未完成任何任务的代理 SHALL 停在起点等待任务。

#### Scenario: 卸货后回停车位
- **WHEN** robot_4 完成任务后没有新任务，且当前不在 `park_4`
- **THEN** robot_4 逐段申请路权驶回 `park_4`

#### Scenario: 刚启动的车不回停车位
- **WHEN** 以 `start_stations:=jn_s,machining home_stations:=park_1,park_2` 启动 2 车，尚未下任何单
- **THEN** robot_1 停在 jn_s、robot_2 停在 machining，不申请驶往停车位的路权

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

### Requirement: 执行让路指令
代理 SHALL 订阅 `/traffic/yield` 中发给自己的指令：`reroute` 时把指令中的资源加入临时绕行集合，按指令给出的新路径（或自行绕开这些资源重新寻路）继续本程；`backoff` 时先驶往指令中的避让点，在那里停留指令给出的时长，再从避让点重新寻路去原目的地；退避还没走完（未到避让点）时执行 `reroute`，SHALL 取消这次退避，以本程目的地为到达站；`pause` 时停在原地不再申请，直到自己持有的路权被运维清除。临时绕行集合 SHALL 在本程结束时清空；带着它重新寻路时到不了当前目标（退避中是避让点，否则是本程目的地），也 SHALL 清空它并再寻路一次，且不改变当前目标。封路不受影响，始终避开；清空后仍无路时停在原地重试。

#### Scenario: 收到改道指令
- **WHEN** robot_2 在 jn_s 等待时收到绕开 `route_node:machining` 的 `reroute` 指令
- **THEN** robot_2 改走不经过 machining 的路径并申请其第一段

#### Scenario: 收到退避指令
- **WHEN** robot_2 收到目标 park_3、停留 5 秒的 `backoff` 指令
- **THEN** robot_2 逐段申请驶往 park_3，到达后停留约 5 秒，再申请去原目的地的路径

#### Scenario: 收到暂停指令
- **WHEN** robot_2 收到 `pause` 指令
- **THEN** robot_2 停在当前站点，不再发出路权申请

#### Scenario: 退避途中收到改道
- **WHEN** robot_4 正退避去 park_2、在 machining 等路权时，收到经 jn_n、packing、jn_nw、storage、jn_sw 到本程目的地 receiving 的 `reroute` 指令
- **THEN** robot_4 不再驶往 park_2，沿新路径到 receiving 后按到达本程目的地处理（装卸停留、回报到站），不再申请下一段

#### Scenario: 临时绕行切断了去目的地的路
- **WHEN** robot_1 先执行绕开 `route_node:jn_s` 去 packing 的 `reroute`，走到 jn_n 又被退避到 park_5，停完后去 packing 的路都要经过 jn_s
- **THEN** robot_1 清空临时绕行，经 jn_s 驶往 packing；若这条路上有封路且没有别的路，则仍不穿过封路，停在原地重试

### Requirement: 幂等请求丢应答时重发
任务代理查询站点、查询路网、查询派给自己的任务、申请路权这四种请求，发出 5 秒仍无应答且服务可用时，SHALL 按原样重发并记一条警告，直到拿到应答；被放弃的那次请求迟到的应答 SHALL 被忽略。拿到应答之前 SHALL NOT 换内容发同一种请求。

#### Scenario: 启动时路网查询的应答丢了
- **WHEN** robot_3 启动后 `/v2/list_station_routes` 的第一次应答丢失
- **THEN** robot_3 约 5 秒后重发，拿到路网后占住起点并就绪

#### Scenario: 已授予但应答丢了
- **WHEN** 路权服务已把一段路授予 robot_2，但应答丢失
- **THEN** robot_2 按原样重发，服务对已被本车持有的资源照样返回授予，robot_2 驶入该段，没有遗留无人释放的资源
