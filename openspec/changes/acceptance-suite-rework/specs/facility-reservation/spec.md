## Purpose

定义任务中枢对设施资源（门、电梯、充电桩等）的预约、执行后保持与释放规则，使经设施动作接口发起的任务在完成、失败或取消后，资源的占用状态与请求中的 `hold_after_action` 一致且可查询。

## ADDED Requirements

### Requirement: 设施动作成功后按 hold_after_action 保持或释放
经 `/v2/execute_facility_action` 发起的设施任务成功完成（状态 `FINISHED`）时，系统 SHALL 按请求的 `hold_after_action` 处理该资源：为真时保持预约，`/v2/list_facility_resources` 中该资源为不可用、状态 `occupied`、占用者为该任务；为假时释放，资源恢复 `available`、占用者为空。路权的释放 SHALL 不受此字段影响。

#### Scenario: 开门后保持占用
- **WHEN** 以 `hold_after_action: true` 对 `receiving_door` 执行 `open` 并等到任务 `FINISHED`
- **THEN** `/v2/list_facility_resources`（`resource_type: door`）中 `receiving_door` 的状态为 `occupied`，占用者为该任务号

#### Scenario: 不保持时完成即释放
- **WHEN** 以 `hold_after_action: false` 对 `freight_elevator` 执行 `call` 并等到任务 `FINISHED`
- **THEN** `freight_elevator` 为可用，状态 `available`，占用者为空

### Requirement: 保持中的设施资源由占用者显式释放
处于 `occupied` 的设施资源 SHALL 保持占用，直到调用 `/v2/release_facility_resource` 且占用者为该占用者或为空（运维强制释放）；以非空的其他占用者释放 SHALL 被拒绝且资源状态不变。释放成功后资源 SHALL 恢复 `available`。

#### Scenario: 占用者释放
- **WHEN** `receiving_door` 被任务 `facility_action_door_alpha` 保持，随后以该占用者调用释放
- **THEN** 响应 `success=True`，资源状态变为 `available`

#### Scenario: 非占用者释放被拒
- **WHEN** 以占用者 `someone_else` 释放被 `facility_action_door_alpha` 保持的 `receiving_door`
- **THEN** 响应 `success=False`，资源仍为 `occupied`

### Requirement: 设施任务失败或取消时释放资源
设施任务失败或被取消时，系统 SHALL 释放该任务预约的设施资源，与 `hold_after_action` 无关。

#### Scenario: 取消后释放
- **WHEN** 以 `hold_after_action: true` 发起的门动作在执行中被取消
- **THEN** 该门恢复 `available`，占用者为空
