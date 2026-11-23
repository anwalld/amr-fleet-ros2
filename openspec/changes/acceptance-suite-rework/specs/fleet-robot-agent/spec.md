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

## ADDED Requirements

### Requirement: 幂等请求丢应答时重发
任务代理查询站点、查询路网、查询派给自己的任务、申请路权这四种请求，发出 5 秒仍无应答且服务可用时，SHALL 按原样重发并记一条警告，直到拿到应答；被放弃的那次请求迟到的应答 SHALL 被忽略。拿到应答之前 SHALL NOT 换内容发同一种请求。

#### Scenario: 启动时路网查询的应答丢了
- **WHEN** robot_3 启动后 `/v2/list_station_routes` 的第一次应答丢失
- **THEN** robot_3 约 5 秒后重发，拿到路网后占住起点并就绪

#### Scenario: 已授予但应答丢了
- **WHEN** 路权服务已把一段路授予 robot_2，但应答丢失
- **THEN** robot_2 按原样重发，服务对已被本车持有的资源照样返回授予，robot_2 驶入该段，没有遗留无人释放的资源
