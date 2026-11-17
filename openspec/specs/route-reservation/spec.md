# route-reservation Specification

## Purpose
管理路网上的路权资源（路段与路口），决定哪台车在什么时候可以驶入哪一段路，保证任意时刻一个资源只有一个占用者，并向任务中枢、车队代理和调度界面提供申请、释放、封路与状态查询接口。

## Requirements

### Requirement: 路权资源模型
系统 SHALL 用两类资源表示路权：路段锁 `route_edge:<a>__<b>`（两站名按字典序排列，与行驶方向无关）和路口锁 `route_node:<站点>`。路权表中每个资源最多有一个占用者；占用者是车辆号、whole_route 模式下任务中枢的任务号，或封路标记 `traffic_block:<原因>`。

#### Scenario: 同一路段两个方向共用一把锁
- **WHEN** 车辆申请 `storage_a → receiving` 这段路
- **THEN** 申请的路段资源为 `route_edge:receiving__storage_a`，与反方向申请的是同一个资源

#### Scenario: 一个资源同一时刻只有一个占用者
- **WHEN** `route_node:storage_a` 已由 robot_1 占用，robot_2 申请包含该资源的一段路
- **THEN** robot_2 不会被授予该资源，路权表中该资源的占用者仍是 robot_1

### Requirement: 路权模式可配置
任务中枢 SHALL 支持参数 `traffic_reservation_mode`，取值 `rolling`（默认）或 `whole_route`；其他取值按 `rolling` 处理并记录警告。当前模式 SHALL 出现在申请应答的 `mode` 字段和 `/traffic/state` 中。

#### Scenario: 未知模式回退为 rolling
- **WHEN** 以 `traffic_reservation_mode:=foo` 启动任务中枢
- **THEN** 任务中枢按 rolling 模式工作，日志中有一条未知模式的警告

### Requirement: rolling 模式按段授予路权
rolling 模式下，车辆从站点 A 前往下一站 B 时 SHALL 申请"路段 A–B + 路口 B"这一组资源（起点未知时只申请路口 B；参数 `traffic_intersection_locks_enabled` 关闭时不含路口锁，默认开启）。只有整组资源都空闲或已归该车所有时才授予，并把整组资源记为该车占用；否则不授予任何一个。

#### Scenario: 整组空闲时授予
- **WHEN** robot_1 申请 `receiving → storage_a`，`route_edge:receiving__storage_a` 与 `route_node:storage_a` 都空闲
- **THEN** 应答 `granted=true`，两个资源的占用者都变为 robot_1

#### Scenario: 部分被占时整组不授予
- **WHEN** robot_2 申请 `packing → storage_a`，其中 `route_node:storage_a` 被 robot_1 占用
- **THEN** 应答 `granted=false`，`route_edge:packing__storage_a` 也不会被 robot_2 占用

#### Scenario: 已持有的资源不算冲突
- **WHEN** robot_1 已持有 `route_node:storage_a`，再申请包含它的一段路且其余资源空闲
- **THEN** 应答 `granted=true`

### Requirement: rolling 模式申请不到时排队
rolling 模式下申请不到时 SHALL 不拒绝，而是把该车排进每个缺的资源的等待队列，应答 `queued=true` 并给出阻塞资源及其占用者。队列按优先级降序、再按到达顺序升序排列。同一辆车同一时刻只等一组资源：新申请替换旧的等待；资源组和优先级都不变的重复申请保留原排位。

#### Scenario: 被占时进入等待队列
- **WHEN** robot_2 申请的 `route_node:storage_a` 被 robot_1 占用
- **THEN** 应答 `granted=false, queued=true`，`blocking_resource_ids` 含 `route_node:storage_a`，`blocking_owner_ids` 含 robot_1

#### Scenario: 高优先级排在前面
- **WHEN** robot_2（优先级 10）先排队，robot_3（优先级 20）后排队等同一资源
- **THEN** 该资源的等待队列中 robot_3 在 robot_2 之前

#### Scenario: 重复申请不丢排位
- **WHEN** 已在排队的 robot_2 以相同资源组和相同优先级再次申请
- **THEN** robot_2 在队列中的位置不变，`/traffic/state` 的新排队计数不增加

### Requirement: 释放后按队列补授予
车辆释放资源（指定资源，或不指定即释放其持有的全部资源）后，系统 SHALL 收集等在这些资源上的车，按优先级降序、到达顺序升序依次尝试授予：整组资源拿得齐的立即授予并移出队列，拿不齐的跳过、不阻塞后面的车。被授予的车 SHALL 在 `/traffic/grants` 上收到 `event=granted` 通知。

#### Scenario: 释放后队首拿到路权
- **WHEN** robot_1 释放 `route_edge:receiving__storage_a` 与 `route_node:storage_a`，robot_2 正在等 `route_node:storage_a` 且它要的其余资源空闲
- **THEN** robot_2 被授予整组资源，释放应答的 `granted_robot_ids` 含 robot_2，`/traffic/grants` 发出 robot_2 的 `granted` 通知

#### Scenario: 拿不齐的等待者被跳过
- **WHEN** 释放后排在前面的 robot_3 仍缺一个被他车占用的资源，排在后面的 robot_2 所需资源全部空闲
- **THEN** robot_2 被授予，robot_3 继续留在队列中

#### Scenario: 释放全部资源
- **WHEN** robot_1 调用释放且不指定资源
- **THEN** robot_1 持有的全部资源被释放，路权表中不再有 robot_1 占用的资源

### Requirement: 定时兜底补授予
系统 SHALL 在解封、清除预约之后立即，并以每秒一次的定时检查，对等待者执行与释放后相同的补授予；资源经其他途径变空闲（如任务结束时的资源清理）时由定时检查兜底，等待者最迟约 1 秒内拿到路权。

#### Scenario: 清除预约后等待者拿到路权
- **WHEN** 运维调用清除预约删掉 robot_1 占用的资源，robot_2 正在等该资源
- **THEN** robot_2 被授予并收到 `granted` 通知

### Requirement: whole_route 模式整条路径一次申请
whole_route 模式 SHALL 保留旧行为作为对照：申请带 `station_path` 时一次申请路径上的全部路段和除起点外的全部路口；拿不全就拒绝，不排队。任务中枢自己的站点运输在 whole_route 模式下于入队时以任务号锁整条路径，任务结束才释放。

#### Scenario: 整条路径有一处被占即拒绝
- **WHEN** whole_route 模式下 robot_2 带 `station_path=[packing, storage_a, receiving]` 申请，`route_node:storage_a` 被占用
- **THEN** 应答 `granted=false, queued=false`，robot_2 不进入任何等待队列

#### Scenario: 入队时锁整条路径
- **WHEN** whole_route 模式下提交一张站点运输工单，其路径资源已被另一任务占用
- **THEN** 提交被拒绝，消息以 `station transport route lock rejected: route resource locked:` 开头并指出资源和占用者

### Requirement: 封路与重新规划
运维 SHALL 能通过 `/v2/block_station_route` 封住一条存在且未被占用的路段（占用者为 `traffic_block:<原因>`），通过 `/v2/unblock_station_route` 解封；只有封路标记可被解封。封住的路段不发放：申请撞上封路时应答 `replan_required=true`，不授予也不排队；封路时已在该路段排队的车 SHALL 被移出队列并在 `/traffic/grants` 收到 `event=replan` 通知。

#### Scenario: 被占用的路段不能封
- **WHEN** `route_edge:receiving__storage_a` 正被 robot_1 占用时调用封路
- **THEN** 封路失败，消息为 `route resource already reserved: ...`

#### Scenario: 申请撞上封路
- **WHEN** robot_2 申请的路段已被封
- **THEN** 应答 `granted=false, queued=false, replan_required=true`

#### Scenario: 封路时通知排队的车改道
- **WHEN** robot_2 正在等某路段，该路段被封
- **THEN** robot_2 被移出等待队列，`/traffic/grants` 发出 robot_2 的 `replan` 通知

#### Scenario: 不能解封车辆占用的路段
- **WHEN** 对一个由车辆或任务占用（而非封路）的路段调用解封
- **THEN** 解封失败，路权表不变

### Requirement: 清除预约
运维 SHALL 能通过 `/v2/clear_traffic_reservation` 按资源号清除一项占用：若是封路标记则只删该资源；若是车辆或任务占用，则释放该占用者持有的全部路权。清除不存在的资源返回失败。

#### Scenario: 清除车辆占用的一项资源
- **WHEN** 对 robot_1 占用的 `route_node:storage_a` 调用清除预约
- **THEN** robot_1 持有的全部路权都被释放，应答 `owner_id=robot_1`

### Requirement: 撤销等待
任务取消时，系统 SHALL 撤掉该车的全部等待，使其不再出现在任何等待队列中，也不会在之后被授予。

#### Scenario: 取消任务后不再排队
- **WHEN** 正在 `WAIT_ROUTE` 的站点运输被取消
- **THEN** 该车从所有等待队列中消失

### Requirement: 交通状态与授予通知话题
系统 SHALL 以 1 Hz 发布 `/traffic/state`：模式、占用表（资源与占用者）、每个资源的等待队列（等待者、优先级、队内位置）以及累计授予次数与新排队次数；SHALL 在 `/traffic/grants` 上发布每一次授予（`granted`）和重新规划（`replan`）通知，包含车辆号、起止站和资源。

#### Scenario: 状态话题反映占用与排队
- **WHEN** robot_1 占用 `route_node:storage_a`，robot_2 在其上排队
- **THEN** 下一条 `/traffic/state` 的占用表含 (`route_node:storage_a`, robot_1)，等待队列含 (`route_node:storage_a`, robot_2, 位置 0)

### Requirement: 申请与释放服务
系统 SHALL 提供 `/v2/request_route_segment`（输入车辆号、起止站、优先级、可选整条路径；输出 granted、queued、replan_required、资源、阻塞资源与占用者、模式、消息）与 `/v2/release_route_segment`（输入车辆号、可选资源列表；输出已释放资源、新被授予的车辆、消息）。申请 SHALL 校验站点与路段存在，车辆号为空时失败。

#### Scenario: 车辆号为空
- **WHEN** 以空 `robot_id` 调用申请
- **THEN** 应答 `granted=false, queued=false`，消息说明需要 robot_id

#### Scenario: 路段不存在
- **WHEN** 申请一条路网中不存在的路段
- **THEN** 应答 `success=false`，不授予也不排队

### Requirement: 旧路权接口保持兼容
`/v2/list_traffic_reservations`、`/v2/detect_traffic_deadlock`、`/v2/block_station_route`、`/v2/unblock_station_route`、`/v2/clear_traffic_reservation` 的服务名和类型 SHALL 保持不变。占用查询返回当前占用表；死锁检测在当前版本只报告"同一站点被两个以上占用者的路段锁触及"的冲突候选，不寻找等待环、不做解除。

#### Scenario: 死锁检测报告冲突候选
- **WHEN** 两个占用者的路段锁共同触及站点 storage_a
- **THEN** `/v2/detect_traffic_deadlock` 返回 `deadlocked=true`，`conflict_station_ids` 含 storage_a，`mission_ids` 含两个占用者
