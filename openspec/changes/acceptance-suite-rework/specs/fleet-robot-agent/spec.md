## MODIFIED Requirements

### Requirement: 空闲回停车位
`return_home_when_idle` 为真（默认）时，没有任务且不在自己停车位的代理 SHALL 按同样的路权规则驶回停车位，但仅限该代理自启动以来至少完成过一个任务；刚启动、尚未完成任何任务的代理 SHALL 停在起点等待任务。

#### Scenario: 卸货后回停车位
- **WHEN** robot_4 完成任务后没有新任务，且当前不在 `park_4`
- **THEN** robot_4 逐段申请路权驶回 `park_4`

#### Scenario: 刚启动的车不回停车位
- **WHEN** 以 `start_stations:=jn_s,machining home_stations:=park_1,park_2` 启动 2 车，尚未下任何单
- **THEN** robot_1 停在 jn_s、robot_2 停在 machining，不申请驶往停车位的路权
