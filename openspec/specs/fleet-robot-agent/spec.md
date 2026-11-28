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
处于取消后等待调度状态的代理 SHALL 原地等待独立移车指令；其余车辆遵循以下空闲行为。`return_home_when_idle` 为真（默认）时，没有任务且不在自己停车位的代理 SHALL 按同样的路权规则驶回停车位，但仅限该代理自启动以来至少完成过一个任务；刚启动、尚未完成任何任务的代理 SHALL 停在起点等待任务。

#### Scenario: 卸货后回停车位
- **WHEN** robot_4 完成任务后没有新任务，且当前不在 `park_4`
- **THEN** robot_4 逐段申请路权驶回 `park_4`

#### Scenario: 刚启动的车不回停车位
- **WHEN** 以 `start_stations:=jn_s,machining home_stations:=park_1,park_2` 启动 2 车，尚未下任何单
- **THEN** robot_1 停在 jn_s、robot_2 停在 machining，不申请驶往停车位的路权

#### Scenario: 取消后驻留
- **WHEN** 已经完成过任务的车辆在途中取消当前任务且启用空闲回位
- **THEN** 即使原定停车位空闲也保持当前位置，直到收到独立移车指令

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

### Requirement: 在当前路段执行目标任务控制
代理 SHALL 按目标和请求代次执行暂停、恢复和取消，停稳且目标匹配后才确认停车终态。

#### Scenario: 行驶中暂停再恢复
- **WHEN** 暂停当前 T，停稳后明确恢复且通行条件满足
- **THEN** 原地暂停，再从原进度继续 T

#### Scenario: 行驶中直接取消或暂停后取消
- **WHEN** 对行驶中或已暂停的 T 发出取消
- **THEN** 确认停稳后结束 T，保留车辆等待调度状态

#### Scenario: 急停中恢复
- **WHEN** 人工暂停和车队急停同时生效时请求恢复
- **THEN** 拒绝且不排队，解除急停仍需再次恢复

#### Scenario: 迟到与重复
- **WHEN** 重复请求或向正在执行 U 的车辆发送 T 的旧请求
- **THEN** 重复无额外作用，旧请求不能改变 U

#### Scenario: 调度中枢重启后查询原命令
- **WHEN** 中枢识别到重复运行标记，原配属和回执仍待对账
- **THEN** 返回待对账及请求原标识，拒绝新派单和远程任务状态写入

#### Scenario: 中枢重启后按原标识查询可证实的回执
- **WHEN** 原分配存在且按任务 ID、执行 ID 和修订查询已确认回执
- **THEN** 从持久记录返回同一回执，或在原代理仍可核对时只读查询原凭据。不能确认时继续报告待对账。待派发列表不得向代理提供可领取的新任务

### Requirement: 车队代理释放与状态上报超时重发
跟随模式到站上报，以及任务代理的路权释放、车辆状态上报和远程任务状态上报，发出 5 秒无应答且服务可用时 SHALL 自动重发直至确认；已取消、超时淘汰或已完成的回调 SHALL 按请求代次忽略。释放 SHALL 按 `service_epoch` 分批携带租约令牌并在清空前暂缓新路权申请；任务状态上报 SHALL 按先进先出顺序逐条确认，不覆盖未确认的 `FINISHED`。
#### Scenario: 释放或任务完成应答丢失后自动恢复
- **WHEN** 任务代理发出路权释放或任务 `FINISHED` 上报时首次服务应答丢失
- **THEN** 代理在约 5 秒后重发并完成确认，不遗留永久路权占用，任务最终记录为 `FINISHED`
#### Scenario: 跟随模式切站隔离迟到到站回执
- **WHEN** 跟随模式车辆在上一站到站上报尚未返回时收到新目标站，随后上一站的迟到成功回执才到达
- **THEN** 代理忽略上一站回执，不会在到达新目标站前误将新目标站标为已报告到站

### Requirement: 任务代理租约与重规划通知按活跃纪元接纳
任务代理 SHALL 仅由匹配当前请求代次的 `RequestRouteSegment` 应答建立或切换活跃 `service_epoch` 并淘汰旧纪元；`/traffic/grants` 通知 SHALL NOT 切换活跃纪元，仅当通知 `service_epoch` 等于当前活跃纪元时才接纳 `replan` 触发或大于当前纪元已见最大代次的 `granted` 令牌，其余迟到通知 SHALL 忽略。
#### Scenario: 持有新实例或新代次租约期间收到迟到的旧授予通知
- **WHEN** 任务代理已通过当前请求应答记录新路权服务实例 `e2` 的租约令牌 `(e2, r2)`，随后收到滞留的旧实例 `/traffic/grants(e1, r1)` 通知或同实例旧代次 `/traffic/grants(e2, r_old)`（`r_old <= r2`）通知，之后车辆驶离并释放该资源
- **THEN** 代理忽略迟到的旧授予通知并保留本地令牌 `(e2, r2)`，后续发出的释放请求携带 `(e2, [r2])` 并成功释放服务端 `e2` 上的锁，不遗留未释放占用
#### Scenario: 重规划通知按活跃实例纪元触发与过滤
- **WHEN** 任务代理已建立当前活跃纪元 `e2` 并在排队等待路权，期间先后收到旧实例滞留的 `event="replan", service_epoch=e1` 通知与当前实例因封路发出的 `event="replan", service_epoch=e2` 通知
- **THEN** 代理忽略旧纪元 `e1` 的迟到重规划通知，并在收到当前纪元 `e2` 的重规划通知时正常触发重新寻路

### Requirement: 路段申请重试期间响应暂停与让路

车队代理在路段申请重试期间收到暂停或让路指令时，SHALL 在下一轮重试前停止当前申请并执行该指令，资源释放与状态上报 SHALL 继续经由租约跟踪与确认重发路径。

#### Scenario: 重试期间收到让路

- **WHEN** 代理正在重试申请下一路段，此时收到 `backoff` 让路指令
- **THEN** 代理放弃当前申请，按让路路线逐段申请并移动

#### Scenario: 重试期间收到暂停

- **WHEN** 代理正在重试申请下一路段，此时收到暂停指令
- **THEN** 代理停止重试并保持停车，直到暂停解除
