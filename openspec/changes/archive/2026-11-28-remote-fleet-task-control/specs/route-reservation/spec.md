## ADDED Requirements

### Requirement: 取消后保持车辆占用直至驶离
车辆 SHALL 在静止及移车交接期间连续保护实际占用。

#### Scenario: 取消后等待移车
- **WHEN** 默认开启空闲回位并有其他任务，取消路段中的 T
- **THEN** 车辆保持静止，冲突申请不能获授；显式移车且驶离后释放原段
