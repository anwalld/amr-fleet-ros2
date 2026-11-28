## Why

Ops 与 Alarm 联合夜跑中，4 车 rolling 有 1151 秒连续停摆。原路网复现确认，退避决策可把车引入正在被环外车辆使用的支路，之后退避车仍等着已有人停车的避让点，等待图无环。

## What Changes

- rolling 准备下发退避时，优先选当前全程可用的解除方案。若原排序中的车可沿可用路径退避，继续由该车让路。
- 划分规划与执行：路径选择读取当前占用和暂扣。每段驶入照常申请完整路权。

## Capabilities

### Modified Capabilities

- `route-reservation`：补充 rolling 遇到被占用的退避路径时的选择规则。

## Impact

修改任务中枢的死锁解除纯函数与 rolling 接线。沿用既有 ROS 服务、消息、分段路权、暂停条件和验收判据，whole_route 延用原有候选选择。共同夜跑继续覆盖阶段 3、5、6 和 Ops、Alarm 专项。
