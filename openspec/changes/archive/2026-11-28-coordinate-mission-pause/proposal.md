# Proposal

## Why

任务中枢目前用暂停标记和无应答控制请求协调导航，多个暂停来源及跨任务迟到请求缺乏可核对的执行结果。

## What Changes

- 按来源仲裁并确认停车。
- 取消后的调度放行。
- 按代次确认请求与目标结果。
- 核对跨层暂停与调度放行。

## Capabilities

### New Capabilities

### Modified Capabilities

- `mission-control`
- `acceptance-validation`

## Impact

涉及 robot_tasks、导航序列执行端和兼容控制服务。任务中枢统一仲裁暂停来源；导航层执行绑定任务代次的命令并回报结果。保留既有 Trigger 名称及类型，旧入口将操作绑定到受理时的目标并经同一仲裁。内部使用新增的有标识接口，禁止旧入口直接解除仍有效的中枢约束。既有无目标标识的外部 Trigger 无法区分网络中迟到的新旧意图，面向指定任务的调用须使用有标识入口。 [探索结论](../../../../../robot/openspec/changes/explore-task-control-boundaries/proposal.md)
