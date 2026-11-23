# mission-control Specification

## Purpose

定义任务中枢的暂停标记如何随导航序列服务的反馈变化，以及导航序列服务在暂停和取消时如何处理 Nav2 目标。本规格不定义暂停的权威方，也不保证任务中枢显示暂停时车一定停着。

## Requirements

### Requirement: 下层发起的暂停随下层恢复而解除
任务运行中、任务中枢未暂停时，由直接调用 `/pause_mission` 带来"已暂停"反馈的，任务中枢 SHALL 显示 `PAUSED`，并在收到下层不再暂停的反馈时解除暂停、恢复显示运行状态。任务派发之前就留在导航序列服务里的暂停不在本要求范围内。

#### Scenario: 直接暂停再恢复导航序列服务
- **WHEN** 任务运行中直接调用 `/pause_mission`，随后调用 `/resume_mission`
- **THEN** 任务中枢先显示 `PAUSED`，恢复后显示 `RUNNING`

### Requirement: 任务中枢发起的暂停标记不随运行反馈清除
任务中枢发起暂停（人工、工作流、避障、定位丢失）时置位的暂停标记 SHALL NOT 因导航序列服务的运行中反馈而清除。标记已由下层反馈置位时，任务中枢再发起暂停、新上报了会触发暂停的避障或收到会触发暂停的定位丢失位姿，该标记 SHALL 改记为任务中枢发起。本要求只约束这个标记：不保证导航序列服务或车仍然停着，也不保证显示的状态一直是 `PAUSED`。

#### Scenario: 通知下层期间收到运行反馈
- **WHEN** 任务中枢因避障暂停任务，下层处理暂停之前又发来一条运行中反馈
- **THEN** 任务中枢的暂停标记仍然置位

### Requirement: 暂停时停止 Nav2 单点导航目标
导航序列服务经 NavigateToPose 执行目标期间被暂停时（含目标等待受理期间），SHALL 取消该 Nav2 目标，目标未结束前持续重发取消，暂停期间 SHALL NOT 发送新目标，反馈 SHALL 报告已暂停而不是运行中。恢复后 SHALL 重发同一目标；被暂停取消的目标 SHALL NOT 计为失败；在取消生效前已经成功的目标 SHALL NOT 重发。

#### Scenario: 行驶中暂停再恢复
- **WHEN** NavigateToPose 目标执行中调用 `/pause_mission`，随后调用 `/resume_mission`
- **THEN** 暂停期间该目标被取消、没有新目标、反馈为已暂停；恢复后同一目标被重发，完成后计为成功

#### Scenario: 等待受理时暂停
- **WHEN** 目标已发出、Nav2 尚未应答时调用 `/pause_mission`
- **THEN** 目标一被受理即被取消

### Requirement: 取消导航序列不使服务退出
导航序列服务 SHALL 对两种取消都正常结束当前序列并继续服务：收到 action 取消请求时以 `CANCELED` 结束；经 `/cancel_mission` 取消而没有 action 取消请求时以 `ABORTED` 结束，结果消息为 `sequence cancelled`。任务中枢 SHALL 把这两种结果都按已取消处理、不触发失败恢复；其他 `ABORTED` 仍按失败处理。

#### Scenario: 经服务取消
- **WHEN** 任务运行中直接调用 `/cancel_mission`
- **THEN** 任务中枢显示 `CANCELED`、不重试该任务，导航序列服务仍能接受下一个目标

#### Scenario: 经 action 取消
- **WHEN** 对执行中的序列发送 action 取消请求
- **THEN** 序列以 `CANCELED` 结束

### Requirement: 暂停时停止 Nav2 航点跟随目标
导航序列服务经 FollowWaypoints 执行期间被暂停时（含目标等待受理期间），SHALL 取消该目标、目标未结束前持续重发取消，反馈 SHALL 报告已暂停，恢复后从当前航点继续。暂停之前已经结束的目标 SHALL NOT 重发。取消与目标结束同时发生时服务 SHALL NOT 退出。

#### Scenario: 航点跟随等待受理时暂停
- **WHEN** 目标已发出、FollowWaypoints 尚未应答时调用 `/pause_mission`
- **THEN** 目标一被受理即被取消，恢复后重发

#### Scenario: 取消没有生效
- **WHEN** 暂停期间取消请求丢失或被拒
- **THEN** 服务继续重发取消，直到目标结束或任务恢复
