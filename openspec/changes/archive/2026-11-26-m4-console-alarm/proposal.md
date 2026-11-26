# Proposal

## Why

Ops 已能下单和控制流程。操作员仍需在其他工具查看故障状态、操作仿真车队急停和判断连接何时恢复。

## What Changes

- 调度台显示任务事件、故障状态和连接恢复状态。
- 为轻量仿真车队提供停止和确认解除操作。界面分别显示服务回执和车队控制状态。
- 提供 Ubuntu 桌面启动入口及可复现的安装验证。
- 与 m4-console-ops 共用下一次正式夜跑，保留原 Ops 对照和十个专项，增加 Alarm 专项。

## Capabilities

### New Capabilities

### Modified Capabilities

- `operator-console` 增加事件、告警、连接恢复和仿真停止。
- `acceptance-validation` 增加同一阶段 7 的 Alarm 独立专项与报告。

## Impact

涉及 Qt Core、RosBridge、仿真车队代理和夜跑。真车急停链路由现有硬件系统负责，远程任务暂停、恢复和取消沿用其独立准备工作。
