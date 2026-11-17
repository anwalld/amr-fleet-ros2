# Tasks

## 1. 逐条核对规格与实现

- [x] 1.1 核对 `route-reservation` 每条需求：对照 `src/robot_tasks/src/traffic_reservation.cpp`、`traffic_service.cpp` 与 `test/traffic_reservation_test.cpp`，每个场景能指出实现位置或测试用例；不符之处改规格措辞使其描述现状，或记入 design.md 的 Risks
- [x] 1.2 核对 `station-transport` 每条需求：对照 `station_transport_workflow.cpp`、`test/station_transport_workflow_test.cpp` 和 `mission_runner_node.cpp` 的滚动派发与 `WAIT_LOAD`/`WAIT_UNLOAD` 消息文本，同上处理
- [x] 1.3 核对 `fleet-robot-agent` 每条需求：对照 `src/robot_simulation/src/fleet_robot_agent_node.cpp`、`src/robot_bringup/launch/fleet_sim.launch.py` 与 `docs/baseline/M2.md` 的压测记录，同上处理
- [x] 1.4 核对 `acceptance-validation` 每条需求：对照 `scripts/run_acceptance_baseline.sh`、`scripts/robot_wait.sh`、`scripts/run_fleet_traffic_stress.sh`、`scripts/fleet_traffic_stress.py`，同上处理

## 2. 校验与集成

- [x] 2.1 运行 `openspec validate backfill-m0-m2-specs --strict`，无错误无警告
- [x] 2.2 确认 `git diff main --stat` 只包含 `openspec/` 下的文件，没有代码、脚本或接口改动
- [x] 2.3 推送分支并开 PR，确认 GitHub CI（`ros2-ci`）通过

## Workflow follow-up

- 运行 `/opsx:verify` 对照实现做一次整体核验，处理其报告。
- PR 合并后运行 `/opsx:archive`，确认 `openspec/specs/` 下生成四份主规格且 Purpose 已带过去。
- 在交接文档记录：OpenSpec 基线已建立，M3 起以 `route-reservation` 为基础写增量。
