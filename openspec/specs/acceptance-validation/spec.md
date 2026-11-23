# acceptance-validation Specification

## Purpose
定义项目自带的整栈验证工具对外承诺的行为：逐用例隔离运行全部验收用例并产出可逐项对比的结果表、在验收中代替人工完成站点运输装卸确认、以及多车路权压测的安全判定与输出，使每个里程碑的结论都来自可复现的真实运行。

## Requirements

### Requirement: 逐用例隔离的验收运行器
`scripts/run_acceptance_baseline.sh <输出目录> [套件...]` SHALL 逐个运行 `check_robot.sh` 中列出的全部验收用例（默认 mission、facility、amr_sim、business、safety、ops 六个套件），单个用例失败不中断后续用例。每个用例运行前后 SHALL 清掉全部 ROS 与 Gazebo 进程，使每个用例都从干净状态开始。

#### Scenario: 失败不中断
- **WHEN** 运行全部套件时某个用例失败
- **THEN** 运行器记录该用例失败后继续运行下一个用例，直到全部跑完

### Requirement: 用例分类与运行环境
运行器 SHALL 按脚本内容把用例分为三类：`self`（脚本自己启动所需节点）、`live`（对运行中的系统做在线检查）、`static`（不依赖运行中的系统）。对 `live` 用例 SHALL 先单独拉起一套 headless 演示栈（不自动开跑演示），等任务中枢的服务连续 3 次可见（最多等 `STACK_WARMUP`×3 秒，默认 135 秒；超时也照常运行用例，并在栈日志记录 `ready=0`），结束后关闭该栈。

#### Scenario: live 用例拿到独立的栈
- **WHEN** 运行一个 live 类用例
- **THEN** 运行器为它新启动一套演示栈，用例结束后该栈被关闭，不影响下一个用例

### Requirement: 结果表与筛选
运行器 SHALL 在 `<输出目录>/results.tsv` 写出表头 `suite case kind status seconds` 与每个用例一行，状态为 `PASS`、`TIMEOUT`（超过 `CASE_TIMEOUT`，默认 600 秒）或 `FAIL(<退出码>)`，并为每个用例保存日志。环境变量 `ONLY_CASES="套件:用例 ..."` 非空时 SHALL 只运行列出的用例。

#### Scenario: 只跑指定用例
- **WHEN** 以 `ONLY_CASES="safety:route_locks"` 运行
- **THEN** `results.tsv` 只有表头和 `safety route_locks` 一行

#### Scenario: 超时记为 TIMEOUT
- **WHEN** 某个用例运行超过 `CASE_TIMEOUT`
- **THEN** 该用例被终止，状态记为 `TIMEOUT`

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
