## ADDED Requirements
### Requirement: 导航序列边界时序收敛
导航序列服务 SHALL 对超出取消等待窗口后迟到受理的下层导航目标自动执行取消，并在 `FollowWaypoints` 暂停解除时保留暂停瞬间已成功完成的目标结果。
#### Scenario: 取消等待超时后下层迟到受理
- **WHEN** 导航序列在等待 Nav2 或 `FollowWaypoints` 受理期间收到取消且超过 2 秒等待窗口退出，随后下层动作服务器才返回已受理句柄
- **THEN** 导航序列服务自动对该迟到句柄发起取消，不遗留无监管的活动导航目标
#### Scenario: 航点跟随在暂停分支切换瞬间恰好成功
- **WHEN** `FollowWaypoints` 目标在进入暂停取消流程时恰好以 `SUCCEEDED` 完成，随后序列解除暂停且未被取消
- **THEN** 导航序列服务直接结算该成功结果，不重复下发已完成的航点
