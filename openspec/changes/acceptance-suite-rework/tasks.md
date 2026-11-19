# Tasks

**验证规则**
- 白天：只跑单测和静态检查：`colcon build/test --packages-select <改动的包>`、`bash -n`、`shellcheck`、lint 用例（栈为 `none`，不起 ROS）。
- 全栈验证（验收用例、压测、场景、冒烟）一律交给夜跑，按 3 夜安排（用户 2026-11-19 确认）：#1 只验证新运行器；#2 一次跑完其余全部改动；#3 修完后全绿、建基线、回查；#4 仅在 #3 未全绿时备用。两夜之间的白天，经用户授权，可用 `ONLY_CASES` 只复跑失败的用例。
- 本变更**不留"修不好就删"的退路**：删除和豁免只能是 design D1、D2 里已经写明的那几条。新冒出的情况要先回来问用户。
- 提交、推送、开 PR 之前，都要先问用户。每个产品修复单独一个提交。

## 1. 运行器基础设施（用例集合不变）

- [x] 1.1 新建 `scripts/acceptance_cases.tsv`，列为 `suite case script stack budget_s timeout_s origin`，把现有 99 个用例逐行登记：`stack` 先填现状（按 r2 的 kind 换算；plc、tf_tree 按 design D1 改正），`budget_s` / `timeout_s` 先用 r2 的耗时按 D5 估算。验证：表有 99 行，`suite:case` 不重复，`script` 列的文件都存在。
- [x] 1.2 `check_robot.sh` 的 `suite_cases` / `case_script` 改为读用例表。验证：`check_robot.sh --list` 列出的用例与改动前逐项相同（diff 为空）。
- [x] 1.3 运行器改为读用例表：取消 `eval` 和 grep 猜类型；实现 design D4 的六种栈配置，各自的启动命令、就绪条件和独立的 ROS_DOMAIN_ID；`results.tsv` 的 `kind` 列写栈名；按每个用例的 `timeout_s` 执行。验证：`bash -n`、`shellcheck` 通过；`DRY_RUN=1 scripts/nightly.sh` 的阶段 3 显示"用例表列出 99 个用例"。
- [x] 1.4 xtrace：通过 `BASH_ENV` 和 `BASH_XTRACEFD` 为每个用例写 `<套件>__<用例>.trace`；用例失败时，日志末尾附上跟踪的最后 20 行。验证：写一个只含 `false` 的临时用例，跑一遍后 trace 存在，日志末尾能看到 `false` 这一行，标准输出里没有混入跟踪行（这个临时用例不提交）。
- [x] 1.5 `robot_wait.sh` 的 `ROS2_CALL_TIMEOUT` 默认值改为 30 秒；新增 `lint:script_timeouts`（栈 `none`），检查用例脚本和公共助手里的 `ros2 service call`、`topic echo --once`、`action send_goal`、`tf2_echo` 都带超时，并给现有的 58 处 `topic echo --once` 补上超时。验证：lint 在本机通过；故意删掉一处超时后，lint 失败并报出文件和行号。
- [x] 1.6 夜跑 #1：全量 nightly，要求阶段 3 的 99 个用例与 r2 一致，或者只多出原因已经清楚的变化（例如 plc、tf_tree 换栈后的结果）。逐条记录在回查记录的草稿里。

## 2. 产品修复：设施资源执行后保持（design D9）

- [x] 2.1 改 `PlanMissionResultWorkflow` 的成功分支：非对桩任务只释放路权，设施预约交给"标为占用"处理；节点里的执行顺序相应调整。改 `mission_result_workflow_test` 中断言两个标志的用例，新增"成功 + 保持"和"成功 + 不保持"两个测最终效果的单测（`FacilityReservationMap` 的最终状态）。验证：`colcon test --packages-select robot_tasks` 全绿。
- [x] 2.2 加强 `facility:actions`、`facility:charging`、`safety:opportunity_charging`、`facility:door_passage` 的断言，使其对应 `specs/facility-reservation` 的各个场景：保持、占用者释放、非占用者被拒、不保持即释放、取消即释放。验证：`bash -n`；全栈部分进夜跑 #2。

## 3. 修复其余失败用例（design D1）

- [x] 3.1 `ops:interfaces_split`：去掉写死的接口数。验证：`lint:` 栈下本机运行通过；临时删掉某个包 CMakeLists 里的一条注册，检查会失败。
- [x] 3.2 换栈：`mission:estimates`、`mission:preflight`、`facility:plc` 改为 `core`；`safety:system_health`、`ops:map_manager`、`ops:map_zones`、`ops:tf_tree` 改为 `mock_bringup`。同时把这些脚本的断言按 design D7 拆开。验证：用例表更新，`bash -n` 通过；进夜跑 #2。
- [x] 3.3 时间竞争与丢消息（design D1，2026-11-19 实施时更正了 recovery、low_battery_dock 的根因）：`mission:runner`、`mission:controls` 改到 `core` 栈，controls 用约 40 个点的长任务，runner 自己调用 `/start_mission_profile`；`mission:recovery` 改为 `self`，用无 Nav2 时稳定失败的导航序列服务触发 `NEEDS_OPERATOR`；`safety:low_battery_dock` 和 `mission:preflight` 的低电量消息改为连续发布。验证：`bash -n`；进夜跑 #2，要求 4 个都 PASS。
- [x] 3.4 `business:picking_wave`：删掉过时的 `report_short_pick` 步骤，断言 `confirm_pick` 直接进入 `SHORT_PICK`、`short_quantity=2`。验证：进夜跑 #2。
- [x] 3.5 `amr_sim:gazebo_entities`：读 `amr_sim_gazebo_entity_bridge_node` 后更正根因（design D1）：对象标签在 `labels_enabled` 为真时常显，amr_demo 默认已改为 false；脚本起栈时传 `labels_enabled:=true`。验证：进夜跑 #2。
- [ ] 3.6 `amr_sim:gazebo_nav2`：让 `/map_server` 生命周期的等待上限和就绪条件与实际启动顺序一致（只加长轮询，不重跑）。验证：夜跑 #2、#3 两夜都 PASS。

## 4. 待定性的两条（design D1、Open Questions）

- [x] 4.1 `amr_sim:visualization`：读 `amr_sim_visualizer_node`，确认 `mission: ` 文字标记该不该发。如果是脚本过时就改脚本；如果是产品缺陷就修产品，加单测，并用 `/opsx:update` 补规格增量（先告诉用户）。验证：结论写进回查记录；进夜跑。
- [x] 4.2 `business:vda5050_mqtt`：读桥脚本即可定性（无需再夜跑诊断）：vda5050 单是站点运输，取货站停在 WAIT_LOAD 等上货确认，M0 补自动确认器时漏了本脚本；桥启动前加 `start_station_auto_confirm`，桥内 `ros2 service call` 补超时。脚本问题，不涉及产品。验证：`bash -n`、`py_compile`；进夜跑 #2。

## 5. 删并与换层（design D2、D10）

- [x] 5.1 删除 `amr_sim:full`、`business:fulfillment_benchmark`、`business:webhook` 的用例表行；脚本文件如果没有别的引用，也一并删除。新建 `docs/baseline/acceptance_case_map.tsv`，为这 3 个用例写 `new_key=-` 和理由。验证：`grep` 确认这几个脚本没有其他引用。
- [x] 5.2 合并为 `business:fleet_dispatch`：三个旧脚本的全部断言都保留（并加强），本地任务只跑一遍；映射表写 3 行。验证：逐条核对旧断言在新脚本里都有对应（核对表附在提交说明里）；进夜跑。
- [x] 5.3 新增 GoogleTest：载入 `industry_scenarios.yaml` 和对应的站点文件，断言 12 个模板的每一步都能解析出有效路径。新增 `business:scenario_workflow_shapes`：两种执行形态各完整跑一个，再对 12 个模板做下单受理检查。删除两个 industry 旧用例，映射表写 2 行。验证：`colcon test --packages-select robot_tasks` 全绿；进夜跑。
- [x] 5.4 把 `interfaces_split`、`operator_static`、`amr_sim:assets` 改名到 `lint:` 套件，栈为 `none`，映射表写 3 行；`legacy_usage` 因带在线审计留在 `ops`（见 design D2）。验证：本机运行这几个 lint 用例全部通过。
- [x] 5.5 单测：用例表变动时，检查映射表里的每个 `new_key` 都在用例表中、每个被删除的键都写了理由。验证：本机运行通过；故意写错一个键时失败。

## 6. 断言加强（design D7）

- [x] 6.1 新增 `lint:assertion_alternation`，并给出白名单（只放格式变体）。验证：在现状上运行，能报出约 121 + 124 处候选，清单存进提交说明。
- [ ] 6.2 按套件拆开并加强断言，依次为 mission、facility、amr_sim、business、safety、ops，每个套件一个提交，跳过第 2、3、5 组已经处理过的脚本。6 个套件都在夜跑 #2 之前写完、一起验证；新暴露的失败按 design D1 的思路处理，查出产品缺陷的单独提交、补规格增量，并先告诉用户。验证：`lint:assertion_alternation` 通过；夜跑 #3 全绿。

## 7. 新增交通与车队用例（specs 里对应的四条要求）

- [x] 7.1 `traffic:block_replan`（`core` 栈）：按规格断言 replan 通知、`replan_required`、解封后获授予。验证：`bash -n`；进夜跑。
- [x] 7.2 `traffic:whole_route_retry_wait`（`core` 栈，`traffic_reservation_mode=whole_route`，关闭死锁解除）：有效期内能检出环，过期后不再检出。验证：同上。
- [x] 7.3 `traffic:deadlock_pause_clear`（`core` 栈，用 `warehouse_stations.yaml`，没有避让点）：断言 `/traffic/yield` 发出 pause、`/traffic/state` 的暂停计数为 1、被冻结的车申请被拒、清除预约后可以再申请。验证：同上。
- [x] 7.4 `fleet:agent_station_task`（`fleet_sim` 栈）：2 单都完成；`/v2/list_remote_tasks` 的状态正确；agent_events 每单各一条；车回到停车位；交通状态里没有残留的路段。验证：同上。

## 8. 接口全覆盖（design D8）

- [x] 8.1 写提取脚本，按 D8 的规则从 `src/` 和 REST 网关提取接口清单，生成 `docs/baseline/interface_coverage.tsv` 的初稿，包括带车号话题的手工登记行。验证：清单数量与这次盘点一致（服务 99、固定名字的话题和动作 38、REST 路由 26），有出入的逐条说明。
- [x] 8.2 补用例：`ops:navigate_sequence_controls`（`core`）、`ops:chassis_mock`（`mock_bringup`）、`ops:manual_takeover`、`ops:path_tracking`（tracking.launch.py）、`/experiment/statistics` 并入 `ops:path_tracking`（tracking.launch.py 带实验节点）、`ops:legacy_entries`（4 个旧入口各调一次，`legacy_usage` 的白名单只放这一个脚本）；把 4 条没覆盖的 REST 路由并进 rest 的两个用例。验证：`bash -n`；进夜跑。
- [x] 8.3 IMU 磁校准：先查 `imu_100d4_parser_node` 有没有不接硬件的输入方式。有，就新增 `ops:imu_calibration`；确实需要硬件的，**先回来请用户决定**要不要登记为例外。验证：结论写进覆盖表的 note 列。
- [x] 8.4 新增 `lint:interface_coverage`，按规格做三项检查。验证：本机运行通过；临时加一个没有登记的服务名，lint 失败。

## 9. 夜跑判定（specs：阶段 3、5、7）

- [x] 9.1 `nightly.sh` 阶段 3：判定改为全部 PASS；套用映射表做对比；报告增加"新增""已删除""超预算"三个部分；找不到上一夜结果时，基准改为 `acceptance_baseline.tsv`（不存在时说明"本夜作为基线"）。验证：用 r2 的 `results.tsv` 和一份构造的新结果，在本机跑阶段 3 的对比函数，输出与预期一致。
- [x] 9.2 阶段 5：任一组完成任务数为 0 就判失败。验证：用 `2026-11-18-noresolve` 的 `summary.json`（4 台车 0 单）做输入，本机运行判定函数，结果为失败。
- [x] 9.3 阶段 7：`run_console_smoke.sh` 冒烟期间开启采集，结束后用 `robot_console_replay` 回放并检查跳过的行数和四类记录，结果写进 `console_smoke.tsv`。验证：用 `test/data/mini.jsonl` 和一份坏行文件，在本机跑校验函数，结果分别为 PASS 和 FAIL。

## 10. 全绿、新基线与回查

- [ ] 10.1 首个全量全绿夜：阶段 1–7 都 PASS，阶段 3 全部用例 PASS。把它的 `results.tsv` 存为 `docs/baseline/acceptance_baseline.tsv`；用实测耗时回填用例表的 `budget_s` / `timeout_s`；报告取回到 `docs/baseline/nightly/`。
- [ ] 10.2 写 `docs/baseline/acceptance-rework.md`：逐条列出 M1–M3 和 M4-core 的验收结论、对应的新用例和首个全绿夜的结果、原结论是否依赖了弱断言；立不住的在原 M*.md 里改正，并注明日期和依据。验证：每条结论都有用例和夜跑报告可查。
- [ ] 10.3 按 M4-core 自己的验证条件（它的 tasks 9.1–9.4），在同一夜的报告里逐项核对，结果写进回查记录，并告诉 M4 会话。验证：回查记录里有 9.1–9.4 的逐项结论。
- [ ] 10.4 运行 `/opsx:verify`，处理发现的问题。

## Workflow follow-up

- 合入顺序：M4-core 的 PR 先合；本变更变基到合入后的 main 上，再单独开 PR（推送和开 PR 前先问用户）。
- 本变更合入后归档，`acceptance-validation` 的主规格随之更新，并新建 `facility-reservation` 主规格。
- 通知 M4 会话：二开计划里 M5 的完成标准"与 M2 逐项无回退"要改为对比 `acceptance_baseline.tsv`（由它和用户确认）；`m4-console-ops` 变基后要按覆盖表补上自己的用例。
