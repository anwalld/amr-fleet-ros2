# route-reservation Specification

## Purpose
管理路网上的路权资源（路段与路口），决定哪台车在什么时候可以驶入哪一段路，保证任意时刻一个资源只有一个占用者，并向任务中枢、车队代理和调度界面提供申请、释放、封路与状态查询接口。

## Requirements

### Requirement: 路权资源模型
系统 SHALL 用两类资源表示路权：路段锁 `route_edge:<a>__<b>`（两站名按字典序排列，与行驶方向无关）和路口锁 `route_node:<站点>`。路权表中每个资源最多有一个占用者；占用者 SHALL 只有两种：车辆号，或封路标记 `traffic_block:<原因>`。rolling 与 whole_route 两种模式下都以车辆号占用，任务号不再作为占用者。资源号只由路权服务生成，并在申请应答、释放应答和交通状态中给出。

#### Scenario: 同一路段两个方向共用一把锁
- **WHEN** 车辆申请 `storage_a → receiving` 这段路
- **THEN** 申请的路段资源为 `route_edge:receiving__storage_a`，与反方向申请的是同一个资源

#### Scenario: 一个资源同一时刻只有一个占用者
- **WHEN** `route_node:storage_a` 已由 robot_1 占用，robot_2 申请包含该资源的一段路
- **THEN** robot_2 不会被授予该资源，路权表中该资源的占用者仍是 robot_1

#### Scenario: whole_route 下站点运输以车辆号占用
- **WHEN** whole_route 模式下任务中枢为车辆 robot_1 的站点运输工单锁定整条路径
- **THEN** 路权表和 `/traffic/state` 中这些资源的占用者是 robot_1，而不是工单的任务号

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
车辆释放资源（指定资源，或不指定即释放其持有的全部资源）后，系统 SHALL 收集等在这些资源上的车，按优先级降序、到达顺序升序依次尝试授予：整组资源拿得齐的立即授予并移出队列，拿不齐的跳过、不阻塞后面的车。但等待已超过参数 `traffic_wait_starvation_s`（默认 20 秒）的等待者，其所需资源中已空闲的部分 SHALL 暂扣给它，不授予排在它后面、优先级不高于它的车。被授予的车 SHALL 在 `/traffic/grants` 上收到 `event=granted` 通知。

#### Scenario: 释放后队首拿到路权
- **WHEN** robot_1 释放 `route_edge:receiving__storage_a` 与 `route_node:storage_a`，robot_2 正在等 `route_node:storage_a` 且它要的其余资源空闲
- **THEN** robot_2 被授予整组资源，释放应答的 `granted_robot_ids` 含 robot_2，`/traffic/grants` 发出 robot_2 的 `granted` 通知

#### Scenario: 拿不齐的等待者被跳过
- **WHEN** 释放后排在前面的 robot_3 仍缺一个被他车占用的资源且等待未超过阈值，排在后面的 robot_2 所需资源全部空闲
- **THEN** robot_2 被授予，robot_3 继续留在队列中

#### Scenario: 等待过久的等待者不再被插队
- **WHEN** robot_3（优先级 20）已等待 25 秒，仍缺一个被他车占用的资源；它需要的另一个资源 `route_node:jn_s` 刚被释放，排在后面的 robot_2（优先级 10）只需要 `route_node:jn_s`
- **THEN** `route_node:jn_s` 不授予 robot_2，保持空闲留给 robot_3；robot_2 在等待图中记为等待 robot_3

#### Scenario: 释放全部资源
- **WHEN** robot_1 调用释放且不指定资源
- **THEN** robot_1 持有的全部资源被释放，路权表中不再有 robot_1 占用的资源

### Requirement: 定时兜底补授予
系统 SHALL 在解封、清除预约之后立即，并以每秒一次的定时检查，对等待者执行与释放后相同的补授予；资源经其他途径变空闲（如任务结束时的资源清理）时由定时检查兜底，等待者最迟约 1 秒内拿到路权。

#### Scenario: 清除预约后等待者拿到路权
- **WHEN** 运维调用清除预约删掉 robot_1 占用的资源，robot_2 正在等该资源
- **THEN** robot_2 被授予并收到 `granted` 通知

### Requirement: whole_route 模式整条路径一次申请
whole_route 模式 SHALL 保留旧行为作为对照：申请带 `station_path` 时一次申请路径上的全部路段和除起点外的全部路口；拿不全就拒绝，不排队。被拒的申请 SHALL 作为该车的"重试等待"记入等待图，保留参数 `traffic_whole_route_wait_ttl_s`（默认 6 秒）后失效，同一车的新申请替换旧记录。任务中枢自己的站点运输在 whole_route 模式下于入队时以执行车辆的车辆号锁整条路径，任务结束才释放该任务锁的、且不被该车其他未结束任务使用的资源。

#### Scenario: 整条路径有一处被占即拒绝
- **WHEN** whole_route 模式下 robot_2 带 `station_path=[packing, storage_a, receiving]` 申请，`route_node:storage_a` 被占用
- **THEN** 应答 `granted=false, queued=false`，robot_2 不进入任何等待队列

#### Scenario: 被拒的申请计入等待图
- **WHEN** whole_route 模式下 robot_2 的整条路径申请因 robot_1 占着 `route_node:storage_a` 被拒
- **THEN** 在 `traffic_whole_route_wait_ttl_s` 内的死锁检测中，等待图含 robot_2 → robot_1 这条边；超过该时间且 robot_2 没有再申请时这条边消失

#### Scenario: 入队时锁整条路径
- **WHEN** whole_route 模式下提交一张站点运输工单，其路径资源已被另一辆车占用
- **THEN** 提交被拒绝，消息以 `station transport route lock rejected: route resource locked:` 开头并指出资源和占用者

#### Scenario: 同一车两张工单共用的资源不提前释放
- **WHEN** whole_route 模式下 robot_1 的两张已入队工单都锁了 `route_node:storage_a`，第一张结束
- **THEN** `route_node:storage_a` 仍由 robot_1 占用，直到第二张工单也结束

### Requirement: 封路与重新规划
运维 SHALL 能通过 `/v2/block_station_route` 封住一条存在且未被占用的路段（占用者为 `traffic_block:<原因>`），通过 `/v2/unblock_station_route` 解封；只有封路标记可被解封。封住的路段不发放：申请撞上封路时应答 `replan_required=true`，不授予也不排队；封路时正在等待、且所申请的那组资源包含该路段的车（即使它排队是因为组内另一个资源被占）SHALL 被移出全部队列并在 `/traffic/grants` 收到 `event=replan` 通知。

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

#### Scenario: 因路口被占而等待时前方路段被封
- **WHEN** robot_1 占用 `route_node:storage_a`，robot_2 申请 packing → storage_a 因此排队，随后空闲的 packing–storage_a 路段被封
- **THEN** robot_2 被移出等待队列并收到 `replan` 通知；申请中不含该路段的等待车不受影响

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
系统 SHALL 以 1 Hz 发布 `/traffic/state`：模式、占用表（资源与占用者）、每个资源的等待队列（等待者、优先级、队内位置）、累计授予次数与新排队次数，以及死锁统计：累计检测次数、累计检出环数、当前等待环上的车、最近一次解除的动作（`reroute`、`backoff` 或 `pause`）、让路车与解除耗时（毫秒），累计已解开环数，最近一次与历史最大的单次检测耗时（微秒），按动作分类的累计解除次数。系统 SHALL 在 `/traffic/grants` 上发布每一次授予（`granted`）和重新规划（`replan`）通知，包含车辆号、起止站和资源。

#### Scenario: 状态话题反映占用与排队
- **WHEN** robot_1 占用 `route_node:storage_a`，robot_2 在其上排队
- **THEN** 下一条 `/traffic/state` 的占用表含 (`route_node:storage_a`, robot_1)，等待队列含 (`route_node:storage_a`, robot_2, 位置 0)

#### Scenario: 状态话题反映一次解除
- **WHEN** robot_1 与 robot_2 成环，robot_2 被选为让路车并改道，环在 800 毫秒后消失
- **THEN** 之后的 `/traffic/state` 中累计检出环数加一，最近一次解除动作为 `reroute`、让路车为 robot_2、耗时约 800 毫秒，`reroute` 的累计次数加一

### Requirement: 申请与释放服务
系统 SHALL 提供 `/v2/request_route_segment`（输入车辆号、起止站、优先级、可选整条路径、可选本程目的地 `goal_station`；输出 granted、queued、replan_required、资源、阻塞资源与占用者、模式、消息）与 `/v2/release_route_segment`（输入车辆号、可选资源列表；输出已释放资源、新被授予的车辆、消息）。申请 SHALL 校验站点与路段存在，车辆号为空时失败；`goal_station` 非空但不是路网中的站点时 SHALL 按为空处理并在消息中说明。

#### Scenario: 车辆号为空
- **WHEN** 以空 `robot_id` 调用申请
- **THEN** 应答 `granted=false, queued=false`，消息说明需要 robot_id

#### Scenario: 路段不存在
- **WHEN** 申请一条路网中不存在的路段
- **THEN** 应答 `success=false`，不授予也不排队

#### Scenario: 不带目的地的旧调用照常工作
- **WHEN** 调用方不填 `goal_station` 申请一段空闲的路
- **THEN** 应答 `granted=true`，与 M2 行为相同

### Requirement: 旧路权接口保持兼容
`/v2/list_traffic_reservations`、`/v2/detect_traffic_deadlock`、`/v2/block_station_route`、`/v2/unblock_station_route`、`/v2/clear_traffic_reservation` 的服务名和类型 SHALL 保持不变。占用查询返回当前占用表。死锁检测 SHALL 报告当前等待图中的环：`deadlocked` 表示是否有环；`mission_ids` 沿用旧字段名，内容为环上车辆号；`conflict_resource_ids` 为构成环的等待边上的资源；`conflict_station_ids` 为这些资源涉及的站点；`descriptions` 每个环一条。只是共用站点而没有循环等待时 SHALL 不报告死锁。

#### Scenario: 两车互等报告死锁
- **WHEN** 死锁解除已关闭，robot_alpha 占着 `route_node:storage_a` 等 `route_node:packing`，robot_beta 占着 `route_node:packing` 等 `route_node:storage_a`
- **THEN** `/v2/detect_traffic_deadlock` 返回 `deadlocked=true`，`mission_ids` 含 robot_alpha 与 robot_beta，`conflict_resource_ids` 含 `route_node:storage_a` 与 `route_node:packing`，`conflict_station_ids` 含 storage_a 与 packing

#### Scenario: 死锁检测报告冲突候选
- **WHEN** 两个占用者的路段锁共同触及站点 storage_a（例如 robot_alpha 占着 `route_edge:receiving__storage_a`、robot_beta 占着 `route_edge:packing__storage_a`），但两车都没有等待
- **THEN** 这种 M2 时代的"冲突候选"不再报告：`/v2/detect_traffic_deadlock` 返回 `success=true, deadlocked=false`

### Requirement: 等待图死锁检测
系统 SHALL 维护以车为节点的等待图：车 A 正在等待（rolling 的等待队列、whole_route 的重试等待，或被暂扣资源挡住）的某个资源由车 B 占用或暂扣给 B，即有边 A → B；封路标记不是节点。图中存在环即为死锁。检测 SHALL 在每次有申请新进入等待时执行一次，并每秒全量执行一次；一次检测 SHALL 找出所有互不相交的环。

#### Scenario: 无环
- **WHEN** robot_2 等 robot_1 占着的资源，robot_1 没有等待
- **THEN** 检测结果无环

#### Scenario: 两车对向
- **WHEN** robot_1 占着 `route_node:A` 等 `route_node:B`，robot_2 占着 `route_node:B` 等 `route_node:A`
- **THEN** 检出一个环，含 robot_1、robot_2

#### Scenario: 三车成环
- **WHEN** robot_1 等 robot_2、robot_2 等 robot_3、robot_3 等 robot_1 各自占着的资源
- **THEN** 检出一个环，含 robot_1、robot_2、robot_3

#### Scenario: 环外有车等待
- **WHEN** robot_1 与 robot_2 成环，robot_3 在等 robot_1 占着的资源
- **THEN** 检出的环只含 robot_1、robot_2，robot_3 不在环上

#### Scenario: 一组资源被两辆车挡住
- **WHEN** robot_1 申请的"路段 + 路口"中路段被 robot_2 占用、路口被 robot_3 占用，robot_3 又在等 robot_1 占着的资源
- **THEN** 检出含 robot_1、robot_3 的环

### Requirement: 选择让路车
检出环且死锁解除开启时，系统 SHALL 在每个环上选一辆让路车：任务优先级最低者；优先级相同时选到 `goal_station` 剩余最短路程最长者（不知道目的地的车视为剩余路程为零）；仍相同时选车辆号字典序最大者。按此顺序排在前面的车若既不能改道也不能退避，SHALL 依次改试下一辆，第一辆能改道或退避的车成为让路车；环上全部车都不能动才按兜底处理。同一个环在一次检测中只选一辆。

#### Scenario: 排第一的车被围住时改由下一辆让路
- **WHEN** robot_2 停在 machining、剩余路程最长，与 jn_n 上的 robot_3 对向成环，jn_s 上的 robot_4 也在等 machining；robot_3 可以退到 park_a
- **THEN** robot_2 两侧路口都被在等它的车占着、无路可走，robot_3 被选为让路车并收到退到 park_a 的 `backoff` 指令

#### Scenario: 优先级最低者让路
- **WHEN** robot_1（优先级 20）与 robot_2（优先级 10）成环
- **THEN** robot_2 被选为让路车

#### Scenario: 同优先级时剩余路程长者让路
- **WHEN** robot_1 与 robot_2 优先级相同，robot_1 到目的地还剩 12 米，robot_2 还剩 30 米
- **THEN** robot_2 被选为让路车

### Requirement: 改道解除
系统 SHALL 先尝试改道：在路网中去掉环上其他车、以及直接或间接在等让路车的车所占用或暂扣的资源（以及封住的路段）后，若让路车从当前站点到 `goal_station` 仍有路径，则撤掉让路车的全部等待，并在 `/traffic/yield` 上向它发出 `action=reroute` 的指令，带上应绕开的资源和新路径。

#### Scenario: 有绕行路线时改道
- **WHEN** 让路车 robot_2 在 jn_s 等 robot_1 占着的 `route_node:machining`，绕开它仍能经外环到达目的地 jn_n
- **THEN** robot_2 从所有等待队列中移除，`/traffic/yield` 发出 robot_2 的 `reroute` 指令，新路径不经过 machining

### Requirement: 退避解除
改道不可行时（没有绕行路径，或不知道让路车的目的地），系统 SHALL 选离让路车当前站点最近、未被占用也未被暂扣、且去程绕得开上述资源的避让点，撤掉让路车的全部等待，并在 `/traffic/yield` 上发出 `action=backoff` 的指令，带上去避让点的路径和停留时间（参数 `traffic_yield_hold_s`，默认 5 秒）。路网中没有可达的空闲避让点时 SHALL 按兜底处理。

#### Scenario: 无法改道时退到避让点
- **WHEN** 让路车 robot_2 的目的地就是 robot_1 占着的站点，最近的空闲避让点是 park_3
- **THEN** robot_2 从所有等待队列中移除，`/traffic/yield` 发出 robot_2 的 `backoff` 指令，目标为 park_3，停留 5 秒

### Requirement: 反复成环兜底
同一组车（环上车辆集合相同）在 30 秒内第三次成环时，或退避找不到避让点时，系统 SHALL 不再自动解除：记一条错误日志，在 `/traffic/yield` 上向环上每辆车发出 `action=pause`，这些车的等待保留但不被授予，直到运维通过 `/v2/clear_traffic_reservation` 清除其中任一辆车的占用。

#### Scenario: 30 秒内第三次成环
- **WHEN** robot_1、robot_2 在 30 秒内第三次成环
- **THEN** 两车都收到 `pause` 指令，`/traffic/state` 最近一次解除动作为 `pause`，此后两车不被授予，直到运维清除其中一辆车的占用

### Requirement: 解除耗时
系统 SHALL 把一次解除的耗时定义为从检出该环到下一次检测中该环的车辆集合不再成环之间的时间，并写入 `/traffic/state`；同时 SHALL 用单调时钟记录每次检测本身的耗时。

#### Scenario: 记录解除耗时
- **WHEN** 一个环在检出后经改道于 1.2 秒后的那次检测中不再存在
- **THEN** 该次解除记录的耗时约为 1.2 秒（不超过检测间隔的误差）

### Requirement: 死锁解除开关
任务中枢 SHALL 支持参数 `traffic_deadlock_resolution_enabled`（默认 `true`）。关闭时只检测、只在状态中统计，不选让路车、不发 `/traffic/yield`，等待环保持原样。

#### Scenario: 关闭解除只检测
- **WHEN** 以 `traffic_deadlock_resolution_enabled:=false` 启动，两车成环
- **THEN** `/traffic/state` 的检出环数增加，`/traffic/yield` 没有消息，两车仍在等待队列中

### Requirement: 避让点配置
路网配置 SHALL 允许把站点标为避让点（站点字段 `yield_point: true`）；未标注时 SHALL 把编号以 `park_` 开头的站点视为避让点，以兼容现有路网。

#### Scenario: 现有路网的停车位可作避让点
- **WHEN** 加载未写 `yield_point` 字段的 `workshop` 路网
- **THEN** park_1 … park_6 被视为避让点
### Requirement: 查询清除预约的影响
路权服务 SHALL 按资源号提供只读的清除预约预览，返回当前占用者、将释放的资源集合和将解除暂停的车辆集合。资源不存在时 SHALL 返回无占用，查询 SHALL NOT 修改占用、暂停分组或等待队列。

#### Scenario: 预览车辆占用
- **WHEN** 查询有多项占用且属于兜底暂停组的 robot_1 的一项资源
- **THEN** 返回 robot_1、它持有的全部资源和会解除暂停的组员，路权和暂停状态保持原状

#### Scenario: 预览单条封路标记
- **WHEN** 查询一个由封路标记占用的路段
- **THEN** 释放范围只包含该资源，解除暂停的车辆集合为空

### Requirement: 按已确认影响清除预约
路权服务 SHALL 提供带前置条件的清除预约入口，接收资源号、预期占用者和已确认的两个集合。服务 SHALL 在执行清理的同一临界区内核对这些值。资源消失、占用者变化或任一集合不匹配时 SHALL 返回冲突且不触发任何清理、解除暂停或补授予。全部匹配时 SHALL 执行已有清理语义和正常补授予。

#### Scenario: 占用者发生变化
- **WHEN** 操作员确认 robot_1 的预览后，资源在提交时已由 robot_2 占有
- **THEN** 返回冲突，robot_2 的路权不被该请求释放

#### Scenario: 占用者相同但影响变化
- **WHEN** 提交时占用者相同，但它持有的资源集合或将解除暂停的车辆集合与已确认内容不同
- **THEN** 返回冲突，本次请求不变更路权或暂停状态

#### Scenario: 预览与执行状态一致
- **WHEN** 提交时资源仍存在，当前占用者及两个集合均与确认内容一致
- **THEN** 服务释放确认范围的占用、解除确认范围的暂停，并按已有路权规则处理等待者
