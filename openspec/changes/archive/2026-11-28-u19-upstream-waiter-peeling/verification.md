# OpenSpec 核验记录

## 设计阶段 `/opsx:verify`（2026-11-28）

基于用户 2026-11-28 02:08 拍板（采纳方案 C 环外上游剥离退避 + U19-3，U19-2 继续单独立项，并入 `codex/main-merge-candidate` 第二阶段实施）及四方合流后实测源码，完成设计评审核验：

| 维度 | 状态 | 核验结论 |
| --- | --- | --- |
| Schema & Strict | PASS | `openspec validate u19-upstream-waiter-peeling --strict` 退出码 `0` |
| Completeness | PASS | 已齐备 `proposal.md`、`design.md`、`specs/route-reservation/spec.md`、`specs/fleet-robot-agent/spec.md`、`tasks.md`（末项 3.3 已单列正式共同夜跑验收并保持未勾选） |
| Coherence | PASS | 全部需求、设计决策 D1–D3 与 8 文件实施范围一致，零 `.srv`/`.msg` 接口变更，不破坏 U18 租约与 P0-07 重启 fail-closed |

### 覆盖源码清单（8 个文件）
- `src/robot_tasks/include/robot_tasks/traffic_deadlock.hpp`
- `src/robot_tasks/src/traffic_deadlock.cpp`
- `src/robot_tasks/include/robot_tasks/traffic_service.hpp`
- `src/robot_tasks/src/traffic_service.cpp`
- `src/robot_tasks/test/traffic_deadlock_test.cpp`
- `src/robot_simulation/include/robot_simulation/agent_yield.hpp`
- `src/robot_simulation/src/fleet_robot_agent_node.cpp`
- `src/robot_simulation/test/agent_yield_test.cpp`

## 实施后核验（2026-11-28，`neon-check` VERDICT: PASS）

- **编译**：VM 隔离工作区 `colcon build --packages-select robot_tasks robot_simulation` 通过（`Summary: 2 packages finished [50.9s]`）。
- **单测**：VM 隔离域 `ROS_DOMAIN_ID=225 colcon test --packages-select robot_tasks robot_simulation` 全绿（`Summary: 614 tests, 0 errors, 0 failures, 0 skipped`），其中：
  - `traffic_deadlock_test` 38/38 PASS（含 `Peeling_Workshop2WayAll8008SixRobotLayouts_ZeroPause`: `layouts=8008, peels=77, pauses=0`、`Peeling_V4FormalZeroEscapeScene_PeelsUpstreamWaiter`、`Peeling_RollingDiagnosticZeroEscapeScene_PeelsUpstreamWaiter`、`Peeling_CycleSolvableInCycle_DoesNotPeel`、`Peeling_NoEscapeAnywhere_FallsBackToPause`、`PeelingTimeoutStartsWhenPeelingBegins`）；
  - `traffic_reservation_test` 55/55 PASS、`traffic_clear_service_test` 3/3 PASS、`agent_yield_test` 10/10 PASS（含 `DecideRouteEntryAction` 全组合）、`agent_lease_test` 5/5 PASS。
- **阶段 6 死锁 9 场景回归**：`run_deadlock_scenarios.sh` 退出 0，`deadlock_scenarios.tsv` 9/9 `PASS`（`head_on`/`ring3`/`outside_wait` 各 3 轮，0 违规）。
- **6 车 `rolling` 隔离压测（5 分钟 / 300s，`workshop_2way`）**：`rc=0`，完成 10 单（吞吐 `120.0/h`），`violations=0`，`deadlocks_detected=3`（2 `reroute` + 1 `backoff`，`deadlock_pause=0`，3/3 resolved），全队最长静止 `2s`。
- **独立审计**：`neon-check`（Gemini 4 Neon）核对 8 文件白名单、无接口变更、U18/P0-07/M4 保护点及全部原始日志，给出 `VERDICT: PASS`。
- **正式共同夜跑 3.3 通过（2026-11-28，`0355FtYr69MW3ThcBLkzXp`）**：修订 `8766f99-main-merge-u19-r2`（基于最新 `origin/main@8766f99` 冻结 1,016 个文件）于北京时间 2026-11-28 18:25:24 终态 **8/8 PASS**，本地复核 `source.manifest` 1,016/1,016 SHA-256 零偏差，`postcheck.json` 零问题（`"issues": []`）。阶段 5 六车 `rolling` 满 30 分钟 62 单（`124.0/h`）、0 违规、0 `pause`、12 次死锁全部解开（6 `reroute` + 6 `backoff`）、全队最长静止 `5s`；阶段 6 死锁 9/9 PASS。原始证据：[报告](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/report.md) · [后验](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/postcheck.json) · [源码清单](../../../docs/baseline/nightly/2026-11-28-main-merge-u19-r2-evidence/source.manifest)。末项 `3.3` 已核销。
