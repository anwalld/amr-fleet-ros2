## Purpose

定义任务中枢对执行中任务的暂停与恢复语义：暂停由谁发起、可以由谁解除，以及任务中枢显示的状态如何跟随导航序列服务的反馈。

## ADDED Requirements

### Requirement: 下层发起的暂停随下层恢复而解除
暂停不是由任务中枢发起、而是由导航序列服务的"已暂停"反馈带来时（例如直接调用 `/pause_mission`），任务中枢 SHALL 显示 `PAUSED`，并在收到下层不再暂停的反馈时解除暂停、恢复显示运行状态。

#### Scenario: 直接暂停再恢复导航序列服务
- **WHEN** 任务运行中直接调用 `/pause_mission`，随后调用 `/resume_mission`
- **THEN** 任务中枢先显示 `PAUSED`，恢复后显示 `RUNNING`

### Requirement: 任务中枢发起的暂停只能由任务中枢解除
任务中枢发起的暂停（人工、工作流、避障、定位丢失）SHALL NOT 因导航序列服务的运行反馈而解除，只能经任务中枢自己的恢复流程解除。下层已暂停时任务中枢再发起暂停，或者出现会触发暂停的避障、定位丢失，该暂停 SHALL 视为任务中枢发起。

#### Scenario: 通知下层期间收到运行反馈
- **WHEN** 任务中枢因避障暂停任务，下层处理暂停之前又发来一条运行中反馈
- **THEN** 任务中枢仍为暂停

#### Scenario: 从下层恢复不能绕过任务中枢
- **WHEN** 经 `/pause_mission_profile` 暂停任务后直接调用 `/resume_mission`
- **THEN** 任务中枢仍显示 `PAUSED`，直到调用 `/resume_mission_profile`

#### Scenario: 下层暂停期间遇到障碍
- **WHEN** 任务被 `/pause_mission` 暂停期间上报了需要暂停的障碍，随后直接调用 `/resume_mission`
- **THEN** 任务中枢仍为暂停；障碍清除时也不会自动恢复这个原本由下层发起的暂停

### Requirement: 暂停时停止 Nav2 单点导航目标
导航序列服务经 NavigateToPose 执行目标期间被暂停时（含目标等待受理期间），SHALL 取消该 Nav2 目标，目标未结束前持续重发取消，暂停期间 SHALL NOT 发送新目标，反馈 SHALL 报告已暂停而不是运行中。恢复后 SHALL 重发同一目标；被暂停取消的目标 SHALL NOT 计为失败；在取消生效前已经成功的目标 SHALL NOT 重发。

#### Scenario: 行驶中暂停再恢复
- **WHEN** NavigateToPose 目标执行中调用 `/pause_mission`，随后调用 `/resume_mission`
- **THEN** 暂停期间该目标被取消、没有新目标、反馈为已暂停；恢复后同一目标被重发，完成后计为成功

#### Scenario: 等待受理时暂停
- **WHEN** 目标已发出、Nav2 尚未应答时调用 `/pause_mission`
- **THEN** 目标一被受理即被取消
