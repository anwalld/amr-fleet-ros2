## Why

四车 rolling 再次停摆。退避规划时 park_2 尚可用，另一辆车先回到车位后，退避车在支路入口持续等待。原始证据只确认停摆和时序，等待边归属来自轨迹与既有复现。

## What Changes

- rolling 优先选择当前可通行且目的地为自己的停车位或未分配停车位的解除方案。
- 将仿真配置中的车辆与停车位映射提供给任务中枢，沿用既有候选排序和逐段授予。

## Capabilities

### Modified Capabilities

- `route-reservation`：退避候选使用停车位分配信息。

## Impact

涉及仿真启动和路权解除决策。其余路权模式沿用当前方案。
