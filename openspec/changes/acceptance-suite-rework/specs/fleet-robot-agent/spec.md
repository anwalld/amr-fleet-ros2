## MODIFIED Requirements

### Requirement: 空闲回停车位
`return_home_when_idle` 为真（默认）时，没有任务且不在自己停车位的代理 SHALL 按同样的路权规则驶回停车位，但仅限该代理自启动以来至少完成过一个任务；刚启动、尚未完成任何任务的代理 SHALL 停在起点等待任务。

#### Scenario: 卸货后回停车位
- **WHEN** robot_4 完成任务后没有新任务，且当前不在 `park_4`
- **THEN** robot_4 逐段申请路权驶回 `park_4`

#### Scenario: 刚启动的车不回停车位
- **WHEN** 以 `start_stations:=jn_s,machining home_stations:=park_1,park_2` 启动 2 车，尚未下任何单
- **THEN** robot_1 停在 jn_s、robot_2 停在 machining，不申请驶往停车位的路权

### Requirement: 执行让路指令
代理 SHALL 订阅 `/traffic/yield` 中发给自己的指令：`reroute` 时把指令中的资源加入临时绕行集合，按指令给出的新路径（或自行绕开这些资源重新寻路）继续本程；`backoff` 时先驶往指令中的避让点，在那里停留指令给出的时长，再从避让点重新寻路去原目的地；退避还没走完（未到避让点）时执行 `reroute`，SHALL 取消这次退避，以本程目的地为到达站；`pause` 时停在原地不再申请，直到自己持有的路权被运维清除。临时绕行集合 SHALL 在本程结束时清空。

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
