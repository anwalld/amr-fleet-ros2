# Proposal

## Why

阶段 3 的 99 个验收用例来自上游，质量不够，已经挡不住回归：

- 最近一夜（r2）是 78/99，21 个失败长期留着。阶段 3 的判定是"和上一夜比没有新增失败"，所以这 21 个用例等于不在测。例子：M3 新增 `TrafficYield.msg` 后，`ops:interfaces_split` 因为接口数写死成 99 而失败，但它本来就在已知失败里，连续三夜都报 PASS。
- 99 个用例的脚本里有 121 处断言写成 `rg "success=True|A|B"`。`rg` 里的 `|` 是"或"，所以这些断言只检查了 success，A、B 这些业务期望从来没有被检查过。
- M1–M4 的功能在阶段 3 里几乎没有覆盖，只能靠阶段 5、6、7 的压测和场景。99 个服务里有 13 个、38 个固定名字的话题和动作里有 15 个，没有任何验收脚本引用。
- 2026-11-19 白天带 xtrace 的诊断查清了失败的根因。大多数是脚本和栈的问题，但其中有一个上游产品 bug：设施动作的 `hold_after_action` 从来没有生效过。

M4-core 已经实现，正等着做全栈验证；M5 要按 M1 的方式拆出 `FleetService` 和 `SnapshotService`。两者都需要一个可信的安全网，所以本变更插在 M4 和 M5 之间。分支叠在 m4 之上，在同一轮夜跑里回查 M1–M3 和 M4-core。

## What Changes

- **零已知失败**：阶段 3 的判定从"没有新增失败"改为"全部用例 PASS"。用例失败后不自动重跑。21 个失败用例修好 20 个，删除 1 个（`amr_sim:full`，它按顺序重跑 6 个已有用例，完全重复）。删除和合并的理由都在 design 里逐条写明。
- **断言加强**：121 处 `success=True|…` 写法的断言全部拆开，每个期望单独断言。其余 124 处或分支逐条审查，只允许用于同一事实的不同输出格式。
- **删并**：
  - 删除 `business:fulfillment_benchmark`（跑一遍 warehouse_fulfillment 再写一个内容固定为 PASS 的 CSV）和 `business:webhook`（整个脚本只是 source 了 callbacks 的脚本）。
  - fleet 的三个用例合并成 `business:fleet_dispatch`。
  - 两个 industry 用例改为在验收里每种执行形态完整跑一个；所有模板都能解析出有效路径，改由新增的 GoogleTest 检查。
  - 四个静态检查归入新的 `lint` 套件。
- **新增用例**：
  - 阶段 3 新增 `traffic:block_replan`、`traffic:whole_route_retry_wait`、`traffic:deadlock_pause_clear`、`fleet:agent_station_task`；
  - 为没有覆盖的接口补用例：navigate_sequence 的控制服务、底盘 mock、人工接管、路径跟踪、experiment 统计、旧提交入口的兼容性、IMU 校准；
  - 新建接口覆盖表 `docs/baseline/interface_coverage.tsv`，加 lint 用例检查它和 `src/` 对得上。
- **运行器**：
  - 新增用例表 `scripts/acceptance_cases.tsv`，`check_robot.sh` 的用例清单改为从这张表生成；
  - 按用例选择栈：`none`、`self`、`core`、`amr_demo`、`mock_bringup`、`fleet_sim`；
  - 每个用例有自己的耗时预算和 timeout，不再统一 600 秒；
  - 每个用例写一份 xtrace 跟踪文件；
  - 每次 ros2 调用都带超时，由 lint 检查。
- **新旧对照**：新增映射表 `docs/baseline/acceptance_case_map.tsv`，切换那一夜的报告仍能和上一夜的旧结果逐项对照。新套件第一次全绿的那一夜存为 `docs/baseline/acceptance_baseline.tsv`。
- **夜跑其他阶段**：阶段 5 增加"每组完成任务数 > 0"和"进程不得中途退出"；阶段 7 冒烟结束后回放冒烟期间的录像并校验。
- **产品修复（对外行为变化）**：设施动作成功完成后，按 `hold_after_action` 保持占用或释放，不再总是释放。诊断尚未定论的 `amr_sim:visualization` 和 `business:vda5050_mqtt`，如果查出是产品缺陷，同样在本变更里修，届时用 `/opsx:update` 补上对应的规格增量。
- **回查**：用新套件重新核实 M1–M3 和 M4-core 的验收结论，写成 `docs/baseline/acceptance-rework.md`；立不住的结论在对应的 M*.md 里改正。M4-core 剩下的夜跑验证任务（9.1–9.4）由本变更的夜跑一并完成。

## Capabilities

### New Capabilities

- `mission-control`：**对外行为变化**——由下层反馈带来的暂停随下层恢复而解除（此前任务中枢一直显示 PAUSED）；任务中枢自己发起的暂停只保证标记不随反馈清除，不保证车停着（直接恢复下层仍会让车继续走）。**对外行为变化**——导航序列服务退到 NavigateToPose 时，暂停会取消 Nav2 目标、恢复后重发（此前只是自己原地等，Nav2 继续开车）。**对外行为变化**——`/cancel_mission` 让序列以 ABORTED（`sequence cancelled`）结束、任务中枢按已取消处理（此前节点抛异常退出）；FollowWaypoints 等待受理时暂停也会取消目标。**对外行为变化**——FollowWaypoints 暂停期间会重发取消。

- `facility-reservation`：设施资源（门、电梯、充电桩等）的预约、执行后保持与释放的规则。目前没有任何规格描述它，本变更从修正 `hold_after_action` 开始建立。

### Modified Capabilities

- `route-reservation`：**对外行为变化**——封路时，申请组里含该路段的等待车都会被移出队列并在 `/traffic/grants` 收到 `replan`。此前只查被封路段自己的队列，而封路只能封空闲路段，所以这个通知在运行中从未发出过（夜跑 #2 的 `traffic:block_replan` 查出）。
- `fleet-robot-agent`：空闲回停车位改为仅在至少完成过一个任务之后（夜跑 #1 发现：给了 `home_stations` 的车一启动就同时驶回停车位，在场景布置好之前自己堵成环，阶段 6 由 9/9 掉到 1/9）。

- `acceptance-validation`：
  - 运行器：用例表、按用例选栈、分档 timeout、xtrace；
  - 阶段 3 判定改为全部 PASS，并用映射表对照；
  - 断言规则与调用超时的 lint；
  - 接口覆盖表；
  - 新增的交通、车队用例；
  - 阶段 5 的任务数下限与进程中途退出判定；阶段 7 的回放校验。

## Impact

- **脚本**：
  - `scripts/run_acceptance_baseline.sh`、`check_robot.sh`、`robot_wait.sh`、`nightly.sh`（阶段 3、5、7）、`run_console_smoke.sh`；
  - 99 个用例脚本里约 60 个要改：加强断言、补超时、改栈或期望值；
  - 新增约 15 个用例脚本和 3 张表（用例表、映射表、覆盖表）。
- **产品代码**：`robot_tasks/src/mission_result_workflow.cpp`、`mission_runner_node.cpp` 中成功分支的资源释放顺序，以及对应单测；新增 industry 模板的 GoogleTest。诊断未定论的两条可能还会涉及 `robot_simulation`。
- **接口**：ROS 服务名和类型都不变。设施动作成功后，资源状态从"总是 available"变为按 `hold_after_action` 决定，这是行为变化。
- **基线与文档**：`docs/baseline/` 新增回查记录、新基线和两张表；M1–M3.md 可能有更正。二开计划里 M5 的完成标准"与 M2 逐项无回退"要改为对比新基线，这条由 M4 会话和用户确认，本变更不改计划文档。
- **验证**：白天只跑单测和 lint。全栈验证交给夜跑：可能要 3–5 夜才能全绿，第一夜全绿即为新基线。
- **分支**：叠在 m4 的 `7f14363` 之上。M4-core 先合入，本变更后合入，两个 PR 分开。`m4-console-ops` 在本变更结束之前不动，之后变基，并按覆盖表补上自己的用例。
- **不在范围内**：
  - 暂停语义（权威方、来源、确认与顺序、派发时的危险检查、取消后是否释放暂停）：用户 2026-11-22 决定转独立变更，是"查出的产品缺陷在本变更里修"的例外。本变更带着 design D16 的已知问题合入；
  - 阶段 1、4、6 的判定；
  - GUI 验收（需要桌面）和真机硬件验收；
  - M4 ops / alarm 自己的用例，由那两个变更自带；
  - 修改二开计划文档。
