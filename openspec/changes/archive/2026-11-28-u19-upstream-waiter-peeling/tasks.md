# Tasks

## 1. 规划器（方案 C）

- [x] 1.1 `traffic_deadlock` 增加第二阶段上游剥离纯函数与 `peeling` 标记，环内可解时行为不变。
- [x] 1.2 `traffic_service` 维护 `ActiveDeadlock::peeling_robot_id`：剥离期冻结 30 秒 3 次计数，出口释放即重算，超时恢复计数。
- [x] 1.3 `traffic_deadlock_test` 新增 `workshop_2way` 8,008 布局穷举与 `v4-formal`、`rolling-diagnostic` 现场复刻用例。

## 2. 代理（U19-3）

- [x] 2.1 `FleetTaskAgent::Route()` 重试循环入口消费 `paused_` 与 `pending_yield_`，释放与上报继续走租约与确认重发路径。

## 3. 验证

- [x] 3.1 VM 隔离域跨包编译与 `robot_tasks`、`robot_simulation` 全量单测通过（`614 tests, 0 errors, 0 failures, 0 skipped`）。
- [x] 3.2 VM 隔离域 `run_deadlock_scenarios.sh` 9/9 通过（0 违规），6 车 `rolling` 5 分钟（300s）隔离压测 10 单（120.0/h）、0 违规、0 `pause` 停摆、3/3 死锁解除（30 分钟满时长留待正式共同夜跑阶段 5 复验）。
- [x] 3.3 纳入基于最新主线的冻结快照并通过正式共同夜跑验收（核对 `source.manifest` 与零问题 `postcheck.json`）
