# Proposal

## Why

M4-core 把车队、任务和路权放进了同一个调度台。操作员仍需切到其他工具下单、控制工作流和处理路权，做完再回到调度台核对结果。

## What Changes

- 提供工作流与车队站点搬运两张下单表单。工作流按订单号查询、暂停、恢复和取消，车队搬运保留任务号与分配车辆，本地执行时用服务返回的任务中枢编号跟踪。
- 从地图操作封路和解封，从车队表启用或停用车辆的调度资格。
- 运维区提供清占用入口。确认前由后端给出当前占用者、将释放的资源和将解除暂停的车辆。提交时后端校验预期占用者和已确认的影响范围，变化时返回冲突并由界面刷新。
- 调度台异步发出请求，分别显示服务处理结果和状态观测结果。回放模式只提供展示。
- 远程车队任务暂停、恢复、取消，以及告警、急停、重连界面和打包，留给后续变更。

## Capabilities

### New Capabilities

### Modified Capabilities

- `operator-console` 增加操作入口、结果反馈与回放保护，更新 M4-core 的 ROS 服务调用边界。
- `acceptance-validation` 将调度台写操作专项纳入阶段 7 判定，并保存可逐项复验的结果。
- `route-reservation` 增加清占用影响查询和带前置条件的清理操作。

## Impact

- 依赖 M4-core 的调度台代码和归档后的 `operator-console` 规格，Ops 归档顺序在 Core 之后。
- 影响 `robot_console_core` 的窗口和模型、`robot_console` 的 RosBridge、`robot_tasks` 的 TrafficService，以及 `robot_interfaces_mission` 的服务定义。
- 复用既有下单、工作流、封路解封与车辆调度资格服务。新增 `/v2/preview_traffic_reservation_clear` 和 `/v2/clear_traffic_reservation_if_matches`，保留 `/v2/clear_traffic_reservation` 的服务类型和调用行为。
- 验证以 QtTest、GoogleTest 和 Ubuntu 仿真栈的调度台操作验收为依据。
