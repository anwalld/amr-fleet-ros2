# station-transport Specification

## Purpose
定义站点运输工单（从取货站把载荷运到卸货站）从入队、行驶、装卸确认到完成或取消的完整行为，包括它如何按段申请路权，以及人工装卸确认这道门控。

## Requirements

### Requirement: 入队门控
提交站点运输工单时，系统 SHALL 依次校验：同一工单未在执行或排队、站点目录已加载、预检通过、取货站到卸货站存在启用的路径、车上载荷为空。任一项不满足即拒绝，且不占用任何路权、不进入队列。rolling 模式下入队只做这些校验，不锁路权。

#### Scenario: 无可用路径被拒
- **WHEN** 取货站与卸货站之间没有启用的路段相连
- **THEN** 提交被拒，消息为 `station transport preflight rejected: no enabled station route`

#### Scenario: 车上已有载荷被拒
- **WHEN** 车上已装有载荷时提交新的站点运输工单
- **THEN** 提交被拒，消息为 `station transport pickup requires an empty robot payload`

#### Scenario: rolling 模式入队不锁路权
- **WHEN** rolling 模式下连续提交两张路径重叠的站点运输工单
- **THEN** 两张都被接受进入队列，路权表中没有因入队而新增的占用

### Requirement: 工单状态机
站点运输工单 SHALL 按以下阶段推进：`QUEUED` → `NAV_TO_PICKUP` → `WAIT_LOAD` → `NAV_TO_DROPOFF` → `WAIT_UNLOAD` → `FINISHED`；终止阶段为 `FINISHED`、`FAILED`、`CANCELED`，另有需人工处理的 `NEEDS_OPERATOR`。每次阶段变化 SHALL 发布在 `/mission_runner/state` 并写入任务事件日志。

#### Scenario: 正常完成
- **WHEN** 一张工单两程导航都成功，上货、卸货都被确认
- **THEN** 事件日志依次出现取货行驶、`WAIT_LOAD`、卸货行驶、`WAIT_UNLOAD`、`FINISHED`

### Requirement: rolling 模式按边派发并等待路权
rolling 模式下每一程（去取货站、去卸货站）SHALL 沿站点路径一条边一个导航目标：先为"这条边 + 下一个路口"申请路权，授予后才发出导航目标；申请被排队时进入状态 `WAIT_ROUTE` 并等授予通知；到达下一站后释放身后的边和上一个路口，再申请下一条边。路权占用者是执行该工单的车辆号。

#### Scenario: 拿不到路权时等待
- **WHEN** 工单要走的下一条边的路口被另一辆车占用
- **THEN** `/mission_runner/state` 变为 `WAIT_ROUTE`，消息以 `waiting for route <from> -> <to>` 开头，车不发出导航目标

#### Scenario: 等到授予后继续
- **WHEN** 处于 `WAIT_ROUTE` 的工单收到对应路段的授予通知
- **THEN** 事件日志记录 `ROUTE_GRANTED`，车发出下一段的导航目标

#### Scenario: 到达后释放身后的路
- **WHEN** 车到达下一站
- **THEN** 它刚走过的路段锁和上一站的路口锁被释放，当前所在站点的路口锁仍由它持有

### Requirement: 遇封路时重新规划
rolling 模式下下一条边被封时，工单 SHALL 绕开被封路段重新寻路；找到不同的路径就按新路径继续并在事件日志记录 `ROUTE_REPLANNED`；找不到时进入 `WAIT_ROUTE`，消息以 `route blocked, waiting for unblock` 开头，等待解封。

#### Scenario: 有绕行路径
- **WHEN** 下一条边被封，且存在绕开它到达本程终点的路径
- **THEN** 事件日志记录 `ROUTE_REPLANNED`，工单沿新路径继续

#### Scenario: 无绕行路径
- **WHEN** 下一条边被封，且不存在其他路径
- **THEN** 状态为 `WAIT_ROUTE`，消息以 `route blocked, waiting for unblock` 开头

### Requirement: 装卸确认门控
车到达取货站后 SHALL 停在 `WAIT_LOAD`，消息格式为 `arrived at pickup station <站点>; waiting for load confirmation`；到达卸货站后停在 `WAIT_UNLOAD`，消息格式为 `arrived at dropoff station <站点>; waiting for unload confirmation`。没有确认就不继续。消息格式是对外约定，验收工具按它解析站点名。

#### Scenario: 到取货站后等上货确认
- **WHEN** 车到达取货站 receiving
- **THEN** `/mission_runner/state` 为 `WAIT_LOAD`，消息为 `arrived at pickup station receiving; waiting for load confirmation`，之后没有确认就一直停在该状态

### Requirement: 上货确认规则
`/v2/confirm_load` SHALL 仅在工单处于 `WAIT_LOAD`、确认的站点等于取货站、载荷号非空、车上尚无载荷且工单未暂停时接受；接受后记录载荷并派发去卸货站的一程。否则拒绝且状态不变。

#### Scenario: 站点不符被拒
- **WHEN** 工单在 receiving 等上货，确认请求的站点是 storage_a
- **THEN** 确认被拒，消息为 `load confirmation station does not match pickup station`

#### Scenario: 暂停时被拒
- **WHEN** 工单已暂停时调用上货确认
- **THEN** 确认被拒，消息说明需要先恢复

#### Scenario: 确认后去卸货站
- **WHEN** 在取货站对正确站点和非空载荷号确认上货
- **THEN** 应答 `success=true, loaded=true`，工单进入去卸货站的一程

### Requirement: 卸货确认规则
`/v2/confirm_unload` SHALL 仅在工单处于 `WAIT_UNLOAD`、确认的站点等于卸货站、车上有载荷且载荷号与上货时一致时接受；接受后工单完成、清空载荷并释放该工单占用的资源；rolling 模式下车辆只继续持有自己当前所在站点的路口锁，其余路权和等待全部放掉。否则拒绝且状态不变。

#### Scenario: 完成后只占当前路口
- **WHEN** rolling 模式下工单在卸货站 storage_a 完成
- **THEN** 路权表中该车只占 `route_node:storage_a`，且不在任何等待队列中

#### Scenario: 载荷号不符被拒
- **WHEN** 在卸货站确认卸货，载荷号与上货时不同
- **THEN** 确认被拒，消息为 `unload confirmation payload does not match loaded payload`

#### Scenario: 确认后完成
- **WHEN** 在卸货站以正确站点和载荷号确认卸货
- **THEN** 工单进入 `FINISHED`，车上载荷清空

### Requirement: 导航失败与取消
导航失败或取消时，若车上没有载荷，工单 SHALL 进入 `FAILED`（失败）或 `CANCELED`（取消）；若车上已有载荷，SHALL 进入 `NEEDS_OPERATOR` 等人工处理，不自动释放。取消已终止的工单 SHALL 被拒绝。取消处于 `WAIT_ROUTE` 的工单 SHALL 撤掉该车的路权等待。

#### Scenario: 载货途中取消
- **WHEN** 已上货、正在去卸货站的工单被取消
- **THEN** 工单进入 `NEEDS_OPERATOR`，消息含 `loaded payload requires operator handling`

#### Scenario: 空车取消
- **WHEN** 去取货站途中的工单被取消
- **THEN** 工单进入 `CANCELED`，其占用的资源被释放

#### Scenario: 取消已完成的工单
- **WHEN** 对 `FINISHED` 的工单调用取消
- **THEN** 取消被拒，消息为 `station transport order is already terminal`
