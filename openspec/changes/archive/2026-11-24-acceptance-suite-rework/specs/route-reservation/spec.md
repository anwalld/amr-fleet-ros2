## MODIFIED Requirements

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
