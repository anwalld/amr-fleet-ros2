# Spec Delta

## ADDED Requirements

### Requirement: 调度台 Alarm 共同验收
阶段 7 SHALL 同时保留 Ops 冒烟与写操作专项，并核验 Alarm 的故障事件、断开恢复、停止及安装。阶段 8 SHALL 报告各专项独立结论和日志位置。

#### Scenario: 共同快照
- **WHEN** 包含 Ops 与 Alarm 的正式夜跑完成
- **THEN** 报告记录每项证据，当且仅当必跑专项与原验收都通过时通过
