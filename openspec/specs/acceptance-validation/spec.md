# acceptance-validation Specification

## Purpose
定义项目自带的整栈验证工具对外承诺的行为：逐用例隔离运行全部验收用例并产出可逐项对比的结果表、在验收中代替人工完成站点运输装卸确认、以及多车路权压测的安全判定与输出，使每个里程碑的结论都来自可复现的真实运行。

## Requirements

### Requirement: 逐用例隔离的验收运行器
`scripts/run_acceptance_baseline.sh <输出目录> [套件...]` SHALL 逐个运行用例表 `scripts/acceptance_cases.tsv` 中列出的全部验收用例（默认全部套件），单个用例失败不中断后续用例。每个用例运行前后 SHALL 清掉全部 ROS 与 Gazebo 进程，使每个用例都从干净状态开始。`scripts/check_robot.sh` 的套件与用例清单 SHALL 来自同一张用例表。

#### Scenario: 失败不中断
- **WHEN** 运行全部套件时某个用例失败
- **THEN** 运行器记录该用例失败后继续运行下一个用例，直到全部跑完

#### Scenario: 两个入口看到同一份清单
- **WHEN** 在用例表中新增一行
- **THEN** 运行器和 `check_robot.sh <套件> all` 都会运行该用例，无需另改脚本

### Requirement: 用例分类与运行环境
每个用例 SHALL 在用例表中显式声明所需的栈：`none`（不起栈）、`self`（脚本自行启动）、`core`（任务中枢默认配置，无 Gazebo）、`amr_demo`（headless 演示栈，不自动开跑演示）、`mock_bringup`（硬件 mock bringup 加地图管理）、`fleet_sim`（2 车轻量仿真）。运行器 SHALL 为需要栈的用例单独拉起该栈、按该栈的就绪条件等待（连续 3 次可见，超时也照常运行并在栈日志记录 `ready=0`），用例结束后关闭该栈。运行器 SHALL NOT 根据脚本内容推断用例类型。

#### Scenario: live 用例拿到独立的栈
- **WHEN** 运行一个声明为 `amr_demo` 的用例
- **THEN** 运行器为它新启动一套演示栈，用例结束后该栈被关闭，不影响下一个用例

#### Scenario: 按声明而不是脚本内容选栈
- **WHEN** 一个脚本只通过公共助手调用服务、正文中没有 `ros2 service` 字样，而用例表声明其栈为 `core`
- **THEN** 运行器为它拉起 `core` 栈后再运行

### Requirement: 结果表与筛选
运行器 SHALL 在 `<输出目录>/results.tsv` 写出表头 `suite case kind status seconds` 与每个用例一行（`kind` 列为该用例声明的栈），状态为 `PASS`、`TIMEOUT`（超过该用例在用例表中的 `timeout_s`）或 `FAIL(<退出码>)`，并为每个用例保存日志。环境变量 `ONLY_CASES="套件:用例 ..."` 非空时 SHALL 只运行列出的用例。

#### Scenario: 只跑指定用例
- **WHEN** 以 `ONLY_CASES="safety:route_locks"` 运行
- **THEN** `results.tsv` 只有表头和 `safety route_locks` 一行

#### Scenario: 超时记为 TIMEOUT
- **WHEN** 某个用例运行超过其 `timeout_s`
- **THEN** 该用例被终止，状态记为 `TIMEOUT`

### Requirement: 用例耗时预算与超时
用例表 SHALL 为每个用例给出 `budget_s` 与 `timeout_s`，`timeout_s` 取 `budget_s` 的 2 倍，再限制在 60–600 秒之间。用例耗时超过 `budget_s` 但未超过 `timeout_s` 时 SHALL 仍按其退出码判定，并在夜跑报告中列为"超预算"。

#### Scenario: 超预算只提醒
- **WHEN** 一个 `budget_s=60` 的用例用 90 秒通过
- **THEN** 该用例状态为 `PASS`，夜跑报告的阶段 3 部分把它列在"超预算"中

### Requirement: 失败用例可从跟踪定位
运行器 SHALL 为每个用例把其全部 bash 进程的命令跟踪写入 `<输出目录>/<套件>__<用例>.trace`，且不混入用例的标准输出。用例失败或超时时，其日志末尾 SHALL 附上跟踪的最后 20 行。

#### Scenario: 静默失败也能定位
- **WHEN** 一个脚本因某条 `rg` 不匹配而退出、没有任何报错输出
- **THEN** 该用例日志末尾显示的跟踪最后一条命令就是那条 `rg`

### Requirement: 阶段 3 以全部通过为判定
夜跑阶段 3 SHALL 仅在本夜 `results.tsv` 完整匹配登记的用例表时判定 PASS：每个登记键恰好一行，所报栈一致，状态全部为 `PASS`。运行器与夜跑 SHALL NOT 自动重跑失败或超时的用例。与上一夜的逐项对比 SHALL 写进报告，但不参与判定。

#### Scenario: 全量结果缺失或重复
- **WHEN** 本夜结果只包含表头、缺少任何已登记用例，或同一用例出现复数结果
- **THEN** 阶段 3 判定为失败，并在报告指出缺失或重复

#### Scenario: 已知失败也判失败
- **WHEN** 某用例上一夜和本夜都是 `FAIL(1)`，其余全部 `PASS`
- **THEN** 阶段 3 判定为失败，报告中该用例不计为"新失败"

#### Scenario: 失败不重跑
- **WHEN** 某用例本夜第一次运行失败
- **THEN** 该用例只运行这一次，结果记为失败

### Requirement: 用例新旧对照
夜跑阶段 3 SHALL 先用映射表 `docs/baseline/acceptance_case_map.tsv`（列 `old_key new_key reason`）把上一夜的结果换算到当前用例键再逐项对比：改名沿用旧状态；多个旧键映射到同一新键时全部为 `PASS` 才算 `PASS`；`new_key` 为 `-` 的列为"已删除"并附理由；上一夜没有的键列为"新增"。找不到上一夜结果时 SHALL 以 `docs/baseline/acceptance_baseline.tsv` 为对比基准。

#### Scenario: 合并用例的对照
- **WHEN** 上一夜 `business:fleet_preview`、`business:fleet_runtime`、`business:fleet_task_pool` 都是 `PASS`，本夜 `business:fleet_dispatch` 为 `PASS`
- **THEN** 报告中 `business:fleet_dispatch` 不出现在状态变化表里

#### Scenario: 删除的用例单独列出
- **WHEN** 上一夜有 `amr_sim:full`，映射表中其 `new_key` 为 `-`
- **THEN** 报告的"已删除"部分列出 `amr_sim:full` 及其理由，它不计入新失败或新通过

### Requirement: 验收脚本的调用超时
用例脚本与公共助手中的每次 `ros2 service call`、`ros2 topic echo --once`、`ros2 action send_goal` 与 `tf2_echo` SHALL 带超时，默认 30 秒（可由 `ROS2_CALL_TIMEOUT` 调整）。不幂等的调用超时后 SHALL 以查询确认结果，SHALL NOT 重发。用例 `lint:script_timeouts` SHALL 在发现任何不带超时的上述调用时失败。

#### Scenario: 漏写超时被拦下
- **WHEN** 某用例脚本新增一行不带 `timeout` 的 `ros2 topic echo --once`
- **THEN** `lint:script_timeouts` 失败，并指出该文件与行号

### Requirement: 断言不得用或分支合并不同期望
用例脚本中 `rg` 模式里的 `|` SHALL 只用于同一事实的不同输出格式（如 `success=True|success: true`），不同期望 SHALL 分别断言。用例 `lint:assertion_alternation` SHALL 在发现白名单之外的或分支时失败。

#### Scenario: 合并的期望被拦下
- **WHEN** 某脚本写 `rg "success=True|assigned_robot_id='robot_2'"`
- **THEN** `lint:assertion_alternation` 失败，并指出该文件与行号

### Requirement: 断言只看服务响应
用例脚本与公共助手 SHALL 只经 `service_response` 调用服务：它只输出响应部分，不含请求回显；没有响应时不输出任何内容并沿用调用的退出码。用例 `lint:service_response` SHALL 在发现脚本直接调用 `ros2 service call` 时失败。

#### Scenario: 请求参数不能让断言通过
- **WHEN** 以 `state_filter: FINISHED` 查询事件而响应里没有任何事件
- **THEN** 对该输出匹配 `FINISHED` 不成立

#### Scenario: 应答丢失
- **WHEN** 服务调用只打印了请求回显、随后超时
- **THEN** `service_response` 输出为空、退出码非 0

### Requirement: 接口覆盖表
`docs/baseline/interface_coverage.tsv` SHALL 为 `src/` 中各节点以字面名称创建的每个服务、发布的话题、动作服务器以及 REST 网关的每条路由登记一行，写明覆盖它的用例键或 `via:/v2/submit_order`。用例 `lint:interface_coverage` SHALL 在出现未登记的接口、登记了不存在的用例、或所列用例脚本未引用该接口时失败。

#### Scenario: 新接口没写用例
- **WHEN** 某节点新增服务 `/v2/foo` 而覆盖表没有对应行
- **THEN** `lint:interface_coverage` 失败并列出 `/v2/foo`

#### Scenario: 旧入口经统一入口覆盖
- **WHEN** 覆盖表中 `/v2/submit_fleet_station_task` 登记为 `via:/v2/submit_order` 且有兼容用例调用它
- **THEN** 该行通过检查

### Requirement: 封路重规划验收
用例 `traffic:block_replan` SHALL 在 `core` 栈上判定：车 B 排队等待车 A 占用的路段时封住该路段，B 被移出队列并在 `/traffic/grants` 收到类型为 `replan` 的通知；此时申请该路段返回 `replan_required=true`；解封并释放 A 后，B 的申请返回 `granted=True`。

#### Scenario: 用例通过
- **WHEN** 运行 `ONLY_CASES="traffic:block_replan"`
- **THEN** `results.tsv` 中该用例为 `PASS`

### Requirement: whole_route 重试等待验收
用例 `traffic:whole_route_retry_wait` SHALL 在 `core` 栈、`traffic_reservation_mode=whole_route`、关闭死锁解除的条件下判定：两车整路申请互相被拒后，在 `traffic_whole_route_wait_ttl_s` 内 `/v2/detect_traffic_deadlock` 返回 `deadlocked=true` 且两车都在 `mission_ids` 中；超过该时长且不再重试后返回 `deadlocked=false`。

#### Scenario: 用例通过
- **WHEN** 运行 `ONLY_CASES="traffic:whole_route_retry_wait"`
- **THEN** `results.tsv` 中该用例为 `PASS`

### Requirement: 死锁兜底暂停与解冻验收
用例 `traffic:deadlock_pause_clear` SHALL 在 `core` 栈（无避让点的站点文件）、开启死锁解除的条件下构造两车对向互等且不执行让路指令，判定：`/traffic/yield` 发出 `action=pause` 且 `cycle_robot_ids` 含两车；`/traffic/state` 的暂停计数为 1；被冻结的车再申请被拒且原因指明已暂停；对其调用 `/v2/clear_traffic_reservation` 后可再次申请。

#### Scenario: 用例通过
- **WHEN** 运行 `ONLY_CASES="traffic:deadlock_pause_clear"`
- **THEN** `results.tsv` 中该用例为 `PASS`

### Requirement: 车队代理派单验收
用例 `fleet:agent_station_task` SHALL 在 `fleet_sim` 栈上经 `/v2/submit_order`（`fleet_station`）提交 2 单并判定：两单都完成；`/v2/list_remote_tasks` 列出这两单且完成后状态为已完成；`/fleet/agent_events` 每单各有一条包含耗时、等路权时间、里程的记录；两车回到各自停车位；交通状态中不再有路段占用。

#### Scenario: 用例通过
- **WHEN** 运行 `ONLY_CASES="fleet:agent_station_task"`
- **THEN** `results.tsv` 中该用例为 `PASS`

### Requirement: 压测每组须完成任务
夜跑阶段 5 SHALL 在任一组 `summary.json` 的完成任务数为 0 时判定失败，即使该组没有安全违规；死锁兜底暂停次数 SHALL 只记录、不参与判定。

#### Scenario: 开局即卡死
- **WHEN** 某组 30 分钟内违规为 0 且完成任务数为 0
- **THEN** 阶段 5 判定为失败，报告指出该组

### Requirement: 压测期间进程不得中途退出
夜跑阶段 5 SHALL 在任一组有进程于关栈之前退出（崩溃或正常退出）时判定失败，即使该组违规为 0 且完成了任务；报告 SHALL 写明组、进程与退出码，并注明该组数据不作数。某组没有可读的启动日志时 SHALL 同样判定失败。关栈期间的退出不参与判定。

#### Scenario: 代理中途崩溃
- **WHEN** 某组运行到第 26 分钟时一个车队代理崩溃，该组仍完成了 56 单、违规为 0
- **THEN** 阶段 5 判定为失败，报告写明该组、该进程与退出码

#### Scenario: 正常关栈
- **WHEN** 压测结束、各进程收到 SIGINT 后退出
- **THEN** 这些退出不算中途退出

### Requirement: rolling 压测不得停摆
夜跑阶段 5 SHALL 在任一 rolling 组出现全队连续 60 秒不动（相邻两次每秒采样位移都不超过 0.05 米，含开局与结尾）或任一车全程完成 0 单时判定失败，报告 SHALL 写明组、原因，并注明该组数据不作数。每组（含 whole_route）的报告 SHALL 列出每车完成单数、单车最长静止与全队最长静止；whole_route 组 SHALL NOT 按这两条判失败。某组的 `state.csv` 或 `tasks.csv` 读不到时 SHALL 判定失败。

#### Scenario: 暂停后整队不动
- **WHEN** 某 rolling 组第 1659 秒起全队不动直到第 1800 秒，此前完成了 58 单、违规为 0
- **THEN** 阶段 5 判定为失败，报告写明该组从第几秒起、连续多少秒不动

#### Scenario: 对照组有车一直饿着
- **WHEN** 某 whole_route 组有一台车全程 0 单，其他车照常完成任务
- **THEN** 阶段 5 不因此判失败，报告里该车显示 0 单和它的最长静止时间

### Requirement: 调度台冒烟附带回放校验
调度台冒烟 SHALL 在冒烟期间按回放格式采集，结束后用 `robot_console_replay` 回放该文件；回放退出码非 0、出现被跳过的行、或缺少 `graph`、`pose`、`traffic`、`snapshot` 任一类记录时，冒烟判定 SHALL 为 `FAIL` 并写明原因。

#### Scenario: 录像损坏
- **WHEN** 冒烟期间采集的文件中有一行无法解析
- **THEN** `console_smoke.tsv` 的 `verdict` 为 `FAIL`，原因注明跳过的行数

### Requirement: 站点运输自动装卸确认
验收脚本 SHALL 能通过 `scripts/robot_wait.sh` 启动后台自动确认器：每 0.5 秒读取一次 `/mission_runner/state`，状态为 `WAIT_LOAD` / `WAIT_UNLOAD` 时从消息中解析取货站 / 卸货站名，调用 `/v2/confirm_load` / `/v2/confirm_unload`。每次读取和调用都 SHALL 带超时（读取 5 秒、调用 10 秒），确认被拒或超时只记日志、不中断确认器。

#### Scenario: 自动确认上货
- **WHEN** 验收中站点运输到达 receiving，状态为 `WAIT_LOAD`
- **THEN** 确认器以站点 receiving 调用上货确认，工单继续去卸货站

#### Scenario: 一次调用挂住不影响后续
- **WHEN** 某次确认调用因丢失应答而超过 10 秒
- **THEN** 该次调用被放弃，确认器继续轮询并处理后续的装卸确认

#### Scenario: 无 EXIT 处理的脚本自动清理
- **WHEN** 一个没有设置 EXIT trap 的验收脚本启动确认器后退出
- **THEN** 确认器随脚本退出被停止

### Requirement: 多车路权压测
`scripts/run_fleet_traffic_stress.sh <车数> <分钟> <模式> [输出目录]` SHALL 拉起多车轻量仿真，用固定随机种子在工作站之间生成同一批订单，保持未完成订单数不超过车数，经 `/v2/submit_order`（`fleet_station`）提交，被拒就稍后重试。路网 SHALL 由环境变量 `NETWORK` 选择（默认 `workshop`），死锁解除开关由 `DEADLOCK_RESOLUTION`（`true`/`false`，默认 `true`）选择；位置参数保持不变。

#### Scenario: 同参数重跑订单序列相同
- **WHEN** 以相同车数、模式和默认种子运行两次
- **THEN** 两次生成的订单（取货站、卸货站及其顺序）相同

#### Scenario: 指定双向路网
- **WHEN** 以 `NETWORK=workshop_2way scripts/run_fleet_traffic_stress.sh 4 30 rolling` 运行
- **THEN** 仿真加载 `workshop_2way` 路网，`summary.json` 记录 `network` 为 `workshop_2way`

### Requirement: 压测安全判定
压测工具 SHALL 以 10 Hz 检查并把以下情况记为违规：同一资源在交通状态中出现两个占用者；同一路段上两车距离不超过 0.5 m；车在某路段上却持续 1.5 秒以上不持有该路段。另 SHALL 记录任意两车的最小距离。

#### Scenario: 同一路段两车过近
- **WHEN** 两台车同时位于同一路段且相距 0.4 m
- **THEN** 记一条 `same_edge_distance` 违规

#### Scenario: 刚授予就出发不算违规
- **WHEN** 车在被授予路段后立即驶入，而 1 Hz 的交通状态在 1.5 秒内才反映这次占用
- **THEN** 不记违规

### Requirement: 压测输出与退出码
压测 SHALL 在输出目录写出 `state.csv`（每秒一行：占用数、排队数、各车位置，以及累计检出环数和按 `reroute`/`backoff`/`pause` 分类的累计解除次数）、`tasks.csv`（任务完成明细）、`violations.csv`、`deadlocks.csv`（每次解除一行：时间、环上车辆、让路车、动作、解除耗时毫秒）、`summary.json`（完成任务数、吞吐、等路权时间、违规数及分类、最小距离，以及死锁检出次数、各解除方式次数、解除耗时均值与最大值、单次检测耗时均值与最大值）和启动日志。死锁指标 SHALL 取自 `/traffic/state` 的死锁统计字段。退出码 SHALL 为 0（无违规）、1（有违规）或 2（车队未就绪）。

#### Scenario: 无违规时退出码为 0
- **WHEN** 一轮压测结束且 `violations.csv` 为空
- **THEN** 脚本退出码为 0，`summary.json` 中 `violations` 为 0

#### Scenario: 死锁指标进入汇总
- **WHEN** 一轮压测中交通状态报告 3 次检出，2 次改道、1 次退避
- **THEN** `summary.json` 中死锁检出次数为 3，`reroute` 为 2、`backoff` 为 1，`deadlocks.csv` 有 3 行

### Requirement: 死锁场景脚本
系统 SHALL 提供一个脚本，在轻量仿真上构造三种场景：两车对向、三车成环、环外有车等待，每种按给定遍数（默认 3）运行。每遍 SHALL 判定：在限定时间内检出环、环在限定时间内解开、环外等待的车不被选为让路车、全程无安全违规。结果 SHALL 写成 `deadlock_scenarios.tsv`（场景、遍次、是否检出、解除动作、解除耗时毫秒、结论），全部通过时退出码为 0。

#### Scenario: 两车对向 3 遍都检出并解开
- **WHEN** 以默认遍数运行两车对向场景
- **THEN** `deadlock_scenarios.tsv` 中该场景 3 行的结论都是 `PASS`，每行都有解除动作和耗时

#### Scenario: 环外车不被选为让路车
- **WHEN** 运行环外有车等待场景
- **THEN** 让路车是环上的车之一，该遍结论为 `PASS`

### Requirement: 交通死锁验收用例按等待环判定
验收用例 `safety:traffic_deadlock` SHALL 在关闭死锁解除的条件下构造两车互等，判定 `/v2/detect_traffic_deadlock` 返回 `deadlocked=true` 且两车都在 `mission_ids` 中；并判定两车只是共用站点、没有等待时返回 `deadlocked=false`。

#### Scenario: 用例通过
- **WHEN** 运行 `ONLY_CASES="safety:traffic_deadlock" scripts/run_acceptance_baseline.sh <输出目录>`
- **THEN** `results.tsv` 中该用例为 `PASS`

### Requirement: 压测与死锁场景可选采集调度台回放
`scripts/run_fleet_traffic_stress.sh` 与 `scripts/run_deadlock_scenarios.sh` SHALL 支持环境变量 `CONSOLE_CAPTURE=1`。开启后，在仿真栈运行期间按调度台回放格式同时采集，压测写到 `<输出目录>/console.jsonl`，场景每遍写到 `<输出目录>/scenario_<场景>_<遍次>.console.jsonl`（与该遍的启动日志同名前缀）。不设置时行为与原来完全相同。采集失败 SHALL NOT 改变压测或场景的判定结果和退出码，原有输出文件的格式与内容 SHALL 保持不变。

#### Scenario: 默认不采集
- **WHEN** 不设置 `CONSOLE_CAPTURE` 运行压测
- **THEN** 输出目录中没有 `console.jsonl`，其余输出与原来相同

#### Scenario: 开启采集
- **WHEN** 设置 `CONSOLE_CAPTURE=1` 运行三车成环场景 1 遍
- **THEN** 输出目录中有 `scenario_ring3_1.console.jsonl`，第一行是 `meta`，其 `source` 写明场景和遍次；文件中有 `graph`、`pose`、`traffic`、`snapshot` 四类记录

#### Scenario: 采集进程异常不影响判定
- **WHEN** 开启采集后，采集进程在中途异常退出
- **THEN** 该遍场景的判定和 `deadlock_scenarios.tsv` 中的结论不受影响

### Requirement: 调度台冒烟脚本
`scripts/run_console_smoke.sh <输出目录>` SHALL 拉起 6 车 rolling 轻量仿真并持续下单，在 offscreen 模式下以冒烟模式运行 `robot_console` 5 分钟（时长可用环境变量调整），把调度台输出的度量写进 `<输出目录>/console_smoke.tsv`，并在表末追加一行 `verdict`。判定为 `PASS` 的条件：调度台退出码为 0、快照与交通状态频率都不低于 0.8 Hz、每车位置更新频率都不低于 5 Hz、6 台车都有位置更新。延迟与帧率只记录，不参与判定。脚本退出前 SHALL 停掉自己拉起的全部进程。

#### Scenario: 冒烟通过
- **WHEN** 仿真栈正常，调度台跑满 5 分钟
- **THEN** `console_smoke.tsv` 含调度台输出的全部度量和 `verdict PASS`，脚本退出码为 0

#### Scenario: 调度台没等到数据
- **WHEN** 仿真栈没有起来，调度台以退出码 2 退出
- **THEN** 脚本仍写出 `console_smoke.tsv`，其中只有 `verdict FAIL` 和失败原因，脚本退出码为 1，且没有残留的仿真进程
