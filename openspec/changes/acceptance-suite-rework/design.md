# Design

## Context

动机见 proposal.md 的 Why。和做法有关的现状：

- 运行器 `run_acceptance_baseline.sh` 用 `sed` + `eval` 从 `check_robot.sh` 里取出 `suite_cases` / `case_script`，再 grep 脚本内容猜用例类型（self / live / static）。live 类一律起一套 amr_demo（带 Gazebo，起栈约 41 秒）。所有用例共用同一个 `CASE_TIMEOUT=600`。
- 上游的 live 脚本不全是按 amr_demo 写的。`estimates`、`preflight`、`fleet_preview` 用的是 `mission_runner.launch.py` 的默认配置（`warehouse_stations.yaml`、`robot_navigation/config/map_zones.yaml`）；`system_health`、`tf_tree` 要求 `bringup.launch.py mode:=hardware backend:=mock`；`map_manager`、`map_zones` 要求单独的 `map_manager.launch.py`。
- 脚本普遍 `set -e` 加 `rg`：不匹配时直接退出、不留任何输出，只看日志定位不到失败在哪一行。
- 夜跑报告只检查 `ros2 service call` 是否带超时。另有 58 处 `ros2 topic echo --once` 没带超时，同样会被 DDS 丢包挂住。
- 诊断数据：2026-11-19 16:24 起在 VM 上用 `env SHELLOPTS=xtrace` 跑了 22 个用例，被测代码 7f14363，日志在 `~/diag/2026-11-19`。

## Goals / Non-Goals

**Goals:**

- 每个失败都能从日志直接定位到出错的那一行；每个用例只在它原本面向的栈上跑。
- 阶段 3 全绿就代表"没有已知问题"，而不是"问题没有变多"。
- 接口一旦新增或改名，覆盖表对不上，当夜就变红。

**Non-Goals:**

- 不缩短阶段 3 的总时长。用户的偏好是总时间可以长，删并只看是否重复。
- 不改阶段 1、4、6 的判定，不改压测和场景的 Python 驱动。
- 不把 live 用例改成共用一套栈：要隔离，不要省时间。

## Decisions

### D1 21 个失败用例逐条处理

根据是 r2 的日志加上 2026-11-19 的诊断。

| 用例 | 根因 | 处理 |
| --- | --- | --- |
| `ops:interfaces_split` | 接口数写死为 99，M3 加了 `TrafficYield.msg` 后实际是 100 | 修：去掉写死的数量，改为逐个核对文件是否注册（脚本里已有这项检查） |
| `mission:estimates` | 期望值按 `warehouse_stations.yaml` 写（1.2 米），amr_demo 用的是 `amr_sim_stations.yaml`（5.4 米） | 修：栈改为 `core` |
| `business:picking_wave` | 上游已把短拣并进 `confirm_pick`，脚本还在多调一次 `report_short_pick` | 修：删掉过时的步骤，断言 `confirm_pick` 直接进入 `SHORT_PICK`、`short_quantity=2` |
| `facility:plc`、`ops:tf_tree` | 被 grep 误判成 static，运行器没给它们起栈 | 修：在用例表里显式写栈。plc 用 `core`；tf_tree 用 `mock_bringup` |
| `safety:system_health` | 需要 cmd_vel_mux、system_monitor，只有 bringup mock 模式里有 | 修：栈改为 `mock_bringup` |
| `ops:map_manager`、`ops:map_zones` | `map_manager_node` 没有被任何总启动文件包含 | 修：栈改为 `mock_bringup`（含 map_manager） |
| `mission:preflight` | 禁行区按 `robot_navigation` 的 `map_zones.yaml` 写，amr_demo 用 `amr_sim_map_zones.yaml`，所以禁行区的单被接受了 | 修：栈改为 `core` |
| `mission:runner`、`mission:controls` | `demo_patrol.yaml` 只有 2 个点、约 1 秒跑完；runner 还假定任务会自动开始 | 修：栈改为 `core`；controls 入队约 40 个点的长任务再暂停、恢复、取消；runner 自己调用 `/start_mission_profile`。诊断时 controls 两轮都通过了，印证了时间竞争 |
| `mission:recovery` | （2026-11-19 实施时更正）它要的是导航失败、重试 1 次后升级为 `NEEDS_OPERATOR`；amr_demo 里导航正常，任务直接 FINISHED | 修：改为 `self`，自起任务中枢和 `use_nav2_action:=true simulate_without_nav2:=false` 的导航序列服务（无 Nav2 时稳定返回失败），断言 `NEEDS_OPERATOR`、重试计数、恢复动作与重置 |
| `safety:low_battery_dock` | （2026-11-19 实施时更正）低电量回桩不依赖任务是否在跑；脚本只 `ros2 topic pub --once` 发一次低电量，大概率在订阅匹配前就退出，消息丢失 | 修：连续发布数次后断言 `dock_return` 出现并跑完；`mission:preflight` 的低电量发布同样改为连续发布 |
| `facility:actions`、`facility:charging`、`safety:opportunity_charging`、`facility:door_passage` | **产品 bug**，见 D9 | 修产品代码；脚本的断言一并加强 |
| `amr_sim:gazebo_entities` | （2026-11-19 实施时更正）14 个对象标签在 `labels_enabled` 为真时每个周期都放到可见位置；上游把 `amr_demo.launch.py` 的 `labels_enabled` 默认改成了 false，脚本起栈时没有显式打开，所以全部停在隐藏位 z=-2（按事件显示的是另一批状态标签） | 修：起栈时传 `labels_enabled:=true`，断言不变 |
| `amr_sim:visualization` | `/amr_simulation/markers` 里一直没有 `mission: ` 文字标记 | 先读 `amr_sim_visualizer_node` 定性：如果是标记被上游去掉了，就改脚本；如果是该发没发，就修产品并补规格增量 |
| `business:vda5050_mqtt` | `vda5050_mqtt_mock_bridge.py` 在它自己的 45 秒超时里没等到结果（用例耗时 49 秒），桥的输出随临时目录一起删掉了 | 先改为失败时保留桥的输出，再定性，处理方式同上 |
| `amr_sim:full` | 按顺序重跑 sensor、slam、nav2、default、gazebo_entities、station_motion 这 6 个已有用例，每个最多 2 次，耗时 519 秒 | **删除**。完全重复，覆盖面不减 |

另有两个偶发失败，也按"要修好"处理：

- `amr_sim:gazebo_nav2`：M4 那一夜用了 123 秒，`/map_server` 的生命周期没在时限内进入 active；诊断那一轮 42 秒就通过了。等待生命周期本来就是轮询状态，加长等待上限、让就绪条件和实际的启动顺序一致，不算重跑。
- `mission:controls`：同上表。

### D2 删并与换层（已由用户确认）

| 处理 | 用例 | 理由 |
| --- | --- | --- |
| 删除 | `business:fulfillment_benchmark` | 只是跑一遍 `check_warehouse_fulfillment.sh`，再写一个内容固定为 PASS 的 CSV |
| 删除 | `business:webhook` | 脚本全文就是 `source check_external_callbacks_and_idempotency.sh`，和 `business:callbacks` 完全一样 |
| 合并 | `fleet_preview` + `fleet_runtime` + `fleet_task_pool` → `business:fleet_dispatch` | 三个各测一组车队服务，又各自完整跑一遍同样的本地 fleet_station 任务。合并后三者的全部断言都保留（并加强），任务只跑一遍 |
| 换层 | `industry_workflows` + `more_industry_workflows` → `business:scenario_workflow_shapes` | 11 个模板只有两种执行形态：两步都是 fleet_station；fleet_station 加 station_sequence。验收里每种形态完整跑一个，再对全部 12 个模板只做下单受理检查。"每个模板都能解析出有效路径"改由新增的 GoogleTest 校验 `industry_scenarios.yaml` |
| 归入 lint | `interfaces_split`、`operator_static`、`amr_sim:assets` | 不需要起栈。改名到 `lint:` 套件下，白天和 CI 也能跑。`legacy_usage` 原列在此，实施时发现它还有一段在运行中的 ROS 图上扫描旧服务客户端的在线审计，无栈时会静默跳过，所以留在 `ops`、仍用 `amr_demo` 栈（2026-11-19 更正） |

合并时的规则：旧用例的每条断言都要出现在新用例里。确实要丢掉某条断言，必须在这份 design 里写明理由。

### D3 用例表取代 grep 猜类型

`scripts/acceptance_cases.tsv` 每行一个用例，列为 `suite case script stack budget_s timeout_s origin`。`origin` 取 `upstream` 或 `rework`。

- `check_robot.sh` 的 `suite_cases` / `case_script` 改为读这张表，运行器也改为读表，不再 `eval` 别的脚本里的函数，也不再 grep 猜类型。
- 备选方案是保留 grep，再加一份"例外清单"。没有采用：两个地方各维护一份，正是这次出现误判的原因。

### D4 栈配置

| 栈 | 怎么起 | 就绪条件（连续 3 次可见） |
| --- | --- | --- |
| `none` | 不起栈 | — |
| `self` | 脚本自己起，并自选 ROS_DOMAIN_ID | — |
| `core` | `ros2 launch robot_tasks mission_runner.launch.py`（默认配置），再加无 Nav2 模式的 `navigate_sequence_server` | `/v2/submit_order`、`/mission_runner/state` |
| `amr_demo` | 现状：`amr_demo.launch.py gui:=false use_rviz:=false auto_demo:=false` | 现状 |
| `mock_bringup` | `bringup.launch.py mode:=hardware backend:=mock`，再加 `map_manager.launch.py` | `/v2/set_cmd_source`、`/v2/list_maps`、`odom` 坐标系 |
| `fleet_sim` | `fleet_sim.launch.py fleet_size:=2 network:=workshop_2way` | `/v2/submit_order`、2 台车的 `/robot_k/odom` |

每个栈都有自己的 ROS_DOMAIN_ID，结束时整组杀掉，然后照旧清理全部 ROS 进程。目前能通过的 live 用例**不换栈**，保持稳定；只有面向别的栈写的失败用例才换。

### D5 耗时预算、timeout 与调用超时

- `budget_s` 取新套件第一夜实测耗时 × 1.5，向上取整到 10 秒。`timeout_s` = 2 × `budget_s`，限制在 60–600 秒之间。在第一夜之前，先用 r2 的耗时估算。
- 超过预算只在报告里提醒，不算失败；超过 timeout 记 TIMEOUT，算失败。
- 脚本里每次 `ros2 service call`、`ros2 topic echo --once`、`ros2 action send_goal`、`tf2_echo` 都必须带 `timeout`。默认 `ROS2_CALL_TIMEOUT` 从 120 秒降到 30 秒：DDS 丢了应答，等多久都等不到，正常的应答几秒内就回来。
- 重试只允许两类：带超时的轮询查询；带同一个 request_id、本身幂等的调用。下单、取消这类不幂等的调用如果超时，用查询去确认结果（查事件或队列），不重发。整个用例失败后**不自动重跑**。

### D6 失败可诊断：xtrace 写入单独文件

运行器为每个用例导出 `BASH_ENV`，指向一个很小的初始化文件。它在用例的每个 bash 子进程里打开 `set -x`，并通过 `BASH_XTRACEFD` 把跟踪写到 `<suite>__<case>.trace`。用例失败时，日志末尾附上 trace 的最后 20 行。

备选方案是诊断时用的 `env SHELLOPTS=xtrace`。没有采用：它会把跟踪混进标准输出和日志，干扰 `rg` 对输出的匹配。

### D7 断言规则

- `rg` 的模式里如果有 `|`，只允许用于同一事实的不同输出格式，例如 `success=True|success: true`。不同事实一律拆成多次 `rg`。
- 新增 `lint:assertion_alternation`：扫描用例脚本，凡是模式里有 `|`、但不在白名单里的，判失败。白名单只放格式变体的写法。

### D8 接口覆盖表

`docs/baseline/interface_coverage.tsv` 的列为 `kind name owner cases note`：

- `kind` 取 `service`、`topic`、`action`、`rest`；
- `cases` 是覆盖它的用例键，可以有多个；
- `note` 写断言要点，或写 `via:/v2/submit_order`（只经统一入口转发的旧入口）。

`lint:interface_coverage` 用和这次盘点相同的规则重新提取接口：`src/*/src/*.cpp` 里的 `create_service` / `create_publisher` / 动作服务器的字面名字，再加上 `scripts/rest_api_gateway.py` 的路由。然后判定：

1. 每个提取出的接口在表里都有一行；
2. 表里的每个用例键都在用例表里存在；
3. 表里的接口确实被它列出的用例脚本（或公共助手）引用到。

带车号的话题不在字面提取的范围里，按节点手工登记在表里。

### D9 产品修复：设施资源执行后保持

现状：成功分支里，`PlanMissionResultWorkflow` 同时置了 `release_all_resources=true` 和 `mark_resource_occupied=true`。`ApplyMissionResultDecision` 先调用 `ReleaseAllResourcesForMission`，把设施预约也一起删掉了，之后的 `MarkResourceOccupiedForMission` 已经找不到预约。结果是 `hold_after_action` 从初始提交起就从没生效过。现有单测只断言两个标志，没有断言最终效果。

改法：成功分支（非对桩任务）只释放路权，设施预约交给 `MarkResourceOccupiedForMission` 按 `hold_after_completion` 处理：为真就标为 `occupied`，为假就释放。

- 失败和取消分支不变，仍然全部释放。
- 补测试：给纯函数加一个"决策 + 执行顺序"的单测；节点层由 4 个设施用例在夜跑里验证。
- 规格见 `specs/facility-reservation/spec.md`。
- 备选方案是调换节点里两步的执行顺序。没有采用：先标占用再全部释放，结果仍是被释放。根子在决策本身就错了：成功时不应该要求释放设施。

### D10 新旧对照

映射表 `docs/baseline/acceptance_case_map.tsv` 的列为 `old_key new_key reason`。阶段 3 每晚都套用这张表：

- 改名：一行，沿用旧用例的状态。
- 合并：多行指向同一个 `new_key`，所有旧用例都 PASS 才算 PASS。
- 删除：`new_key` 写 `-`，报告里单独列为"已删除"并附上理由，不参与比较。
- 新增：上一夜没有这个键，记为"新增"。
- 上一夜如果已经是新的键，查表自然查不到，按原键比较。所以不需要套件版本标记。

判定是本夜 `results.tsv` 全部 PASS；对比结果只写进报告。阶段 3 找不到上一夜结果时，对比基准从 `m2_acceptance.tsv` 改为 `acceptance_baseline.tsv`。

### D11 回查

`docs/baseline/acceptance-rework.md` 记录以下内容：

- M1–M3 和 M4-core 各自的验收结论，都取自 M*.md。
- 每条结论对应新套件里的哪些用例，以及首个全绿夜的结果。
- 原结论是不是依赖了被拆开的弱断言。

立不住的结论在原文档里改正，并注明改正的日期和依据。M4-core 的 tasks 9.1–9.4 按它自己的验证条件，在同一夜里核对。

## Risks / Trade-offs

- [断言加强后，一批目前"通过"的用例会变成失败，范围可能比预计的大] → 加强按套件分提交，但都在夜跑 #2 之前写完、一起验证；每个用例都有 xtrace、互相隔离，一夜失败很多也能逐个定位。新暴露的产品问题按 D9 的方式处理，并用 `/opsx:update` 补规格增量。
- [`mock_bringup` 和 `core` 是新的栈组合，就绪条件可能不准] → 栈日志记录 `ready=` 和耗时，夜跑 #2 之后逐个核对就绪条件。
- [IMU 磁校准的服务和话题可能离不开真实串口] → 实施时先查解析节点有没有不接硬件的输入方式。确实需要硬件的，回来请用户决定是否在覆盖表里登记为例外，不自行豁免。
- [映射表只能对照这一次的删并，以后的删并要人记得维护] → `lint:interface_coverage` 只管接口；用例表改动时，单测检查映射表里的 `new_key` 都存在于用例表中。
- [叠在 m4 之上：m4 如果变基，本分支要跟着变基] → m4 归 M4 会话。用户已承诺 ops 分支在本变更结束前不动；m4 若必须改，先和本会话对好。
- [全绿需要多夜] → 压缩为 3 夜（另留 1 夜备用）：白天写代码，只跑单测和 lint；两夜之间经用户授权，白天用 `ONLY_CASES` 只复跑失败用例，减少整夜重跑。

## Migration Plan

1. 先上运行器的基础设施（用例表、栈配置、xtrace、超时 lint），用例集合不变。第一夜要求 99 个用例的结果和 r2 一致，或者只多出原因已经清楚的变化。
2. 修失败用例和产品 bug、做删并、加强断言、新增用例，全部在夜跑 #2 之前写完，#2 一次验证；#2 之后的白天经授权只复跑失败用例。
3. 夜跑 #3（目标为首个全量全绿夜）：存为 `acceptance_baseline.tsv`，用实测值回填 `budget_s`，写回查记录，核对 M4-core 的 9.x 任务。
4. 回退：用例表和映射表都是数据；运行器和 `check_robot.sh` 读的是同一张表，回退就是撤销对应提交（运行器、产品修复各自独立的提交），不另设开关。

## Open Questions

- `amr_sim:visualization`、`business:vda5050_mqtt` 是脚本过时还是产品缺陷，读代码后定性。两种结果都已有处理路径（D1），不影响任务拆分。
