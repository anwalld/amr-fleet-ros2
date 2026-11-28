# Proposal

## Why

调度台可追踪远程搬运，代理尚缺对指定任务执行控制及回报执行结果的契约。

## What Changes

- 在当前路段执行目标任务控制。
- 取消后保持车辆占用直至驶离。
- 展示远程控制回执与观测结果。
- 独立核对远程停车与移车。

## Capabilities

### New Capabilities

### Modified Capabilities

- `fleet-robot-agent`
- `route-reservation`
- `operator-console`
- `acceptance-validation`

## Impact

涉及 robot_interfaces_fleet、robot_tasks 的车队服务、robot_simulation 的任务代理以及 M4 调度台。实施时以合入 Ops/Alarm 的基线集成；保留原下单、查询和状态上报服务的类型。 [探索结论](../../../../../robot/openspec/changes/explore-task-control-boundaries/proposal.md)
