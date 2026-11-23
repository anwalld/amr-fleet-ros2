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

补充（2026-11-22）：`ros2 service call` 会回显请求，对整段输出断言会被请求参数骗过。所有用例经 `service_response` 调用服务，由 `lint:service_response` 强制。

更正 D1 中 `facility:door_passage` 的根因：除 D9 的产品 bug 外，脚本对含请求回显的文件 `rg FINISHED`，第一次轮询就通过，查门时任务还在跑，所以看到 `reserved`。三次运行的跟踪都是只轮询一次。

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

### D12 产品修复：NavigateToPose 回退模式暂停停车

现状：FollowWaypoints 不可用时退到 NavigateToPose，该分支暂停时只阻塞自己，不取消 Nav2 目标。避障另有速度闸门，人工和定位丢失暂停没有，所以车会继续走。

改法：NavigateToPose 没有暂停接口，只能取消再重发。每一步由纯函数 `PlanNav2GoalStep` 决定：

- 暂停且目标未结束就取消，每秒重发一次（服务应答会丢）；
- 等待受理时不阻塞在暂停上，拿到句柄立即取消；序列被取消时最多再等 2 秒句柄；
- 恢复后等旧目标的结果：成功就不重发，否则重发。不区分 CANCELED 与 ABORTED，真走不通的目标重发后会照常失败；
- 取消与目标结束撞上时客户端抛 `UnknownGoalHandleError`，按"已结束"处理。

备选是暂停时也置 `safety_stop`。没有采用：Nav2 仍在执行，会判定卡住并触发恢复行为或超时失败。

验证：`nav2_goal_step_test`；`ops:navigate_sequence_nav2_pause` 用假 NavigateToPose 服务器，不代表真实 Nav2 的减速表现。

### D13 产品修复：取消与 FollowWaypoints 暂停竞态

现状（读代码与 rclcpp_action 头文件得出）：`canceled()` 只能在收到 action 取消请求后调用，`/cancel_mission` 没有这个请求，节点会抛异常退出；action 取消回调先置标志、目标后进入 CANCELING，也有同样的窗口。FollowWaypoints 等待受理时暂停不取消，取消撞上目标结束时客户端抛异常。

改法：

- 没有取消请求时 `abort()` 并带共用常量 `kSequenceCancelledMessage`，任务中枢两条结果路径都按已取消处理。备选是在接口里加取消字段，或服务给自己发 action 取消：前者改公共接口，后者多一次会丢的往返，都没有采用。
- 取消回调不置标志，各循环看 `is_canceling()`。
- 两个 Nav2 分支共用受理等待和安全取消。FollowWaypoints 的恢复语义（从当前航点重发）不变。

验证：`sequence_cancel_test`；`ops:navigate_sequence_cancel` 与 `ops:navigate_sequence_controls`（任务中枢一侧）。

### D14 阶段 5：进程中途退出即失败

现状：`2026-11-18-r2`（4 台 rolling，`bad_alloc`）和 `2026-11-19-rework2`（6 台 rolling，`BadParamException`）各有一个车队代理中途崩溃，两夜阶段 5 都判 PASS，那两组的吞吐也进了报告。

改法：压测脚本关栈前在 `launch.log` 写一行标记，`stress_process_exits.py` 把标记之前的进程退出列出来，阶段 5 据此判失败。旧日志没有标记，退而用第一条 `signal_handler` 行。崩溃根因不在本项，另查；在此之前阶段 5 可能因此失败，这是预期的。受影响的已发布数字在 10.2 回查时更正。

验证：对已有各夜的压测日志离线运行，只报出上述两组。

### D15 产品修复：FollowWaypoints 暂停期间重发取消

现状：FollowWaypoints 暂停只发一次取消，丢了或被拒时 Nav2 继续开车。

改法：用 `PlanNav2GoalStep` 每秒重发取消，直到目标结束、恢复或序列被取消。

验证：`nav2_goal_step_test`；`ops:navigate_sequence_cancel`。

### D16 暂停语义转独立变更：撤回与已知问题

决定（用户 2026-11-22）：暂停语义不在本变更里继续修。撤回 `504621f` 中"取消后释放暂停"的一半，保留重发取消；删去 `ops:navigate_sequence_controls` 中"任务中枢暂停不能被下层恢复解除"和"取消后不继承暂停"两组断言。前者断言的是错误的安全性质，后者对应被撤回的行为，这是对"不删断言换绿灯"的一次明确例外。`6305b4d`、`504621f` 提交说明里与此相反的表述以本节为准。

撤回理由：
- 取消结果回来之前新按下的暂停，通常会被自动恢复抹掉；恢复晚到时也会盖掉下一个任务的暂停；
- `/cancel_mission`、低电回桩、抢占下单都会因此解除暂停、推进队列，车自动开走；
- 释放之后、派发之前出现的障碍不再有暂停挡着；
- 仿真循环取消时会先报一次运行中，"直接暂停 + 直接取消"多半根本不释放，没有达到原目的。

撤回后的行为：暂停中取消，导航序列服务保持暂停；下一个任务（含低电回桩、抢占单）派发后停着，要人工恢复。空闲时 `/resume_mission_profile` 会被拒绝。

已知问题一，2026-11-22 读代码核对过，均未实测：
- 直接调用 `/resume_mission` 能让任务中枢暂停的车继续走，`HoldByRunner` 只改标记、不会再让下层停。期间显示也不可靠：rolling 到下一段显示行驶阶段，任务走完显示 FINISHED 并推进队列；
- 派发先清暂停标记，遗留在下层的暂停随后被记成下层发起，直接恢复时连显示一起解除，危险仍在也一样。所以遗留的暂停不是可靠的危险保护；
- 派发不检查避障和定位丢失。空闲时上报的障碍不会触发暂停；
- 暂停、恢复、取消请求不等应答、不重试，两个服务之间没有顺序：请求丢失或先恢复后暂停的乱序都会让显示和实际不一致；
- 带运动的仿真在暂停中被取消时，先发一帧运动速度再停；
- 带运动的仿真把暂停时间计入目标超时（默认 45 秒，amr_demo 90 秒），暂停太久时恢复后该目标判失败。

已知问题二，仅来自 2026-11-22 的交接记录，尚未复核：
- FollowWaypoints 的取消与成功撞上时，恢复后仍会从当前航点重发；
- `/cancel_mission` 靠结果消息字符串识别，直接的 action 客户端看到的是 ABORTED；
- 等路权期间不能暂停，避障和定位丢失也不触发暂停，拿到路权就起步；
- 序列取消时只向 Nav2 发一次取消，等待受理超过 2 秒时取消不到目标；
- 目标恰好结束时到达的暂停会被下一个任务继承；
- 迟到的 `/cancel_mission` 可能取消掉下一个目标；
- 避障只在带速度闸门的栈里有兜底（`fleet_sim` 没有），人工和定位暂停在闸门里没有对应项；
- 关栈时导航序列节点会因发布反馈抛异常退出。

独立变更要先定：谁是权威、暂停来源集合、请求确认与顺序、派发和等路权时的危险检查、取消后是否释放、怎样断言"车真的停了"。

### D17 产品修复：车队代理让路后越界与永久无路

现状（M3 引入，2026-11-22 读代码、压测日志查出）：
- 退避还没走到避让点时收到改道，代理换了路径却没清避让点，走完新路径仍不算到达，读 `path_[size]`。`2026-11-18-r2`（4 台 rolling）、`2026-11-19-rework2`（6 台 rolling）因此崩溃（D14 中"另查"的根因），`2026-11-18`（4 台 rolling）静默读到错误站名；
- 改道留下的临时绕行只在本程结束时清，它切断去目标的路时 `Route()` 永远原样重试。同一组 robot_1 报 13 次 `no path park_1 -> storage` 直到结束；不越界也能触发。

改法：
- 改道时清掉避让点；取下一站用 `.at()`，越界即抛异常退出。没有采用"报错后继续走"：阶段 5 看不见 ERROR 日志，故障会变成静默；
- 带绕行找不到路时清掉绕行再找一次，封路始终避开。没有采用"退避停完时清"：改道指令过期、自行寻路失败时也会走进同一个死角。

风险与局限：
- 放开绕行后可能再次成环，30 秒内第三次会兜底暂停；
- 阶段 5 对暂停只记录不判定，暂停后整队不动在夜跑 #3 及以前仍会 PASS（D19 起 rolling 组按停摆判失败）；
- 越界退出只有阶段 5 会判失败，阶段 3、6、7 看不见；
- 只有封路可走时仍停在原地重试，期间不处理新的让路指令。

受影响的数字：`2026-11-18` 4 台 rolling 108、`2026-11-18-r2` 4 台 rolling 76、`2026-11-19-rework2` 6 台 rolling 112（单/时）整行作废，在 10.2 更正。同一次核对还发现 M3 中 6 台 rolling 的 66、116 在暂停后整队不动（与本缺陷无关），也在 10.2 更正。

验证：`agent_yield_test`、`agent_route_test`；夜跑 #3 阶段 5。

### D18 产品修复：车队代理幂等请求丢应答时重发

现状（夜跑 #3 查出）：代理的站点、路网、任务、路权四种请求各用一个"在途"标志，只在应答回调里清，没有超时。阶段 6 `outside_wait` 第 1 遍有一次 `/v2/list_station_routes` 应答丢失（服务端有告警），robot_3 一直没就绪。缺陷是读代码确定的；这一遍由它造成是推断（告警不含客户端）。

改法：5 秒没有应答就按原样重发（`agent_request.hpp`）。前三种是只读查询；路权申请是幂等的：已被本车持有照样授予，重复排队保留原位次。没有采用"放弃后按当前状态重新申请"：服务端可能已按原请求授予，换了内容这份授予就没人释放。

不在本次范围：释放路权、上报任务与车队状态仍是发出即忘（丢了请求会留下占用或旧状态）；跟随模式（`follow_fleet_station`）另有三个没有超时的在途标志。

验证：`agent_request_test`；夜跑 #4 阶段 6。

### D19 阶段 5：rolling 停摆即失败；whole_route 只列不判

现状：阶段 5 只看完成单数大于 0 和进程退出，暂停后整队冻结仍然 PASS（M3 的 66、116），一台车全程 0 单也 PASS（夜跑 #3 4 台 whole_route 的 robot_1）。

改法：`stress_stalls.py` 读 `state.csv`、`tasks.csv`。rolling 组全队连续 60 秒不动、或任一车 0 单即失败。60 秒的依据：截至夜跑 #3 各夜 25 组满 30 分钟的 rolling 里，正常 19 组最长 13 秒，冻结 6 组最短 129 秒。

whole_route 只列不判，算法也不改：
- 它是对照组，按规格拿不全就拒绝、不排队；
- 防饿死（`traffic_wait_starvation_s` 的暂扣）只作用于等待队列，所以只对 rolling 生效，whole_route 下长路径的车可能一直饿着。夜跑 #3 的 robot_1 拿着一单要一次锁 8 个站的路线，按每秒采样回放，1800 秒里能确定整条空闲的只有 7 秒（符合饿死；不能完全排除请求丢应答，见 D18）。M3 第三遍和夜跑 #2 的 6 台 whole_route 也各有一台车全程 0 单；
- whole_route 的平均等路权只统计完成的单，一直没完成的单不计入，数值偏低。

局限：全队没有都停时，rolling 里单车中途卡死但此前已完成过单，这条判据看不见。

验证：对已有各夜离线回放只报出 6 组冻结；用构造数据回放阶段 5 的判定函数。

### D20 夜跑 #3 其余失败：脚本修复、测试隔离与取证

- `amr_sim:rviz_topics`：`ros2 topic echo` 默认把字符串截到 128 字符，`target:` 一行被截掉；取标记时加 `--full-length`。#2 之后归因成"中文被转义"是错的。
- `ops:navigate_sequence_cancel`：断言全过，清理时 `wait` 不返回。按进程关系推断卡住的是假 Nav2 的优雅关停（没有复现）。假 Nav2 改为 SIGTERM 直接结束；两个用它的用例清理时最多等 5 秒，还有进程活着就点名并判失败，这样被测节点退不掉也会暴露。
- `test_sim_model_runtime` 主车漂移（#2 0.095、#3 0.023，阈值 0.02）：根因是串话，不是滑行，`4ad597c` 的归因是错的。阶段 2 并行跑 `robot_simulation` 与 `robot_navigation` 的运行时测试，两边都在默认 ROS 域、共用 `/odom`、`/cmd_vel`、`/scan` 和同名 TF。证据：留存日志里与导航测试重叠且仿真正常启动的 15 次全部有 `TF_OLD_DATA`（其中 3 次断言失败），不重叠的 4 次都没有；改之前两包并行 10 次，10 次有告警、3 次漂移失败（0.027、0.081、0.247）。改法：4 个包的 5 个 launch 测试用 `run_test_isolated.py` 各自分配独立的域；阈值不动。没有采用串行跑阶段 2：别处并行跑 colcon test 仍会串。没有 `find_package(ament_cmake_ros)`：它会打开 `BUILD_SHARED_LIBS`，改变产品库的链接方式。
- `mission:reprioritize`：任务端显示已发出，导航序列服务没有开始执行的迹象，推断目标请求丢失。先在发出、受理、拒绝和服务端收到处各记一行，用例内部期限改为从脚本开始算 40 秒并打印任务事件。不加目标重发：它不幂等，可能让车执行两次；目标确认与超时留给暂停语义那个独立变更一并考虑（D16）。

局限：另有 70 处 `ros2 topic echo` 沿用默认截断，没有逐处检查；reprioritize 若再丢请求，#4 仍会失败。

### D21 第 4 轮后按断言重新核验

- 阶段 3 按整张用例表核对本轮结果，每个键只能出现一次，栈必须与声明一致。缺行、重复、错栈、未知键或无效耗时都判失败。完整夜跑固定使用登记表，`ONLY_CASES` 和其他用例表仍可用于单独调用运行器。
- `mock_bringup` 的 TF 探针在有界采样结束后检查输出，避免 `grep -q` 关闭管道造成 SIGPIPE。第 4 轮 6 个该栈探针都记为未就绪，仍分别执行了用例。
- 两个需要特殊任务中枢参数的死锁用例都走 `core`，运行器按用例配置 whole_route、TTL 和解除开关，或关闭路口锁开启解除。封路用例在再次申请前验证等待队列已移除车辆。死锁成员只检查 `mission_ids` 或 `cycle_robot_ids`。车队代理在收尾时验证每单恰好一条事件且交通状态确实给出资源列表。
- `scenario_workflow_shapes` 受理检查涵盖 12 个行业模板，补 `campus/full_mail_route`。已有的独立站点序列执行用例保留。旧接口在线审计在没有图、没有节点或节点查询失败时判失败。
- 第 4 轮保留为原始运行记录。变更后的用例先做无 ROS 栈的坏输入验证，再以完整第 5 轮核对新判据及运行环境。

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
