## MODIFIED Requirements

### Requirement: 多车路权压测
`scripts/run_fleet_traffic_stress.sh <车数> <分钟> <模式> [输出目录]` SHALL 拉起多车轻量仿真，用固定随机种子在工作站之间生成同一批订单，保持未完成订单数不超过车数，经 `/v2/submit_order`（`fleet_station`）提交，被拒就稍后重试。路网 SHALL 由环境变量 `NETWORK` 选择（默认 `workshop`），死锁解除开关由 `DEADLOCK_RESOLUTION`（`true`/`false`，默认 `true`）选择；位置参数保持不变。

#### Scenario: 同参数重跑订单序列相同
- **WHEN** 以相同车数、模式和默认种子运行两次
- **THEN** 两次生成的订单（取货站、卸货站及其顺序）相同

#### Scenario: 指定双向路网
- **WHEN** 以 `NETWORK=workshop_2way scripts/run_fleet_traffic_stress.sh 4 30 rolling` 运行
- **THEN** 仿真加载 `workshop_2way` 路网，`summary.json` 记录 `network` 为 `workshop_2way`

### Requirement: 压测输出与退出码
压测 SHALL 在输出目录写出 `state.csv`（每秒一行：占用数、排队数、各车位置，以及累计检出环数和按 `reroute`/`backoff`/`pause` 分类的累计解除次数）、`tasks.csv`（任务完成明细）、`violations.csv`、`deadlocks.csv`（每次解除一行：时间、环上车辆、让路车、动作、解除耗时毫秒）、`summary.json`（完成任务数、吞吐、等路权时间、违规数及分类、最小距离，以及死锁检出次数、各解除方式次数、解除耗时均值与最大值、单次检测耗时均值与最大值）和启动日志。死锁指标 SHALL 取自 `/traffic/state` 的死锁统计字段。退出码 SHALL 为 0（无违规）、1（有违规）或 2（车队未就绪）。

#### Scenario: 无违规时退出码为 0
- **WHEN** 一轮压测结束且 `violations.csv` 为空
- **THEN** 脚本退出码为 0，`summary.json` 中 `violations` 为 0

#### Scenario: 死锁指标进入汇总
- **WHEN** 一轮压测中交通状态报告 3 次检出，2 次改道、1 次退避
- **THEN** `summary.json` 中死锁检出次数为 3，`reroute` 为 2、`backoff` 为 1，`deadlocks.csv` 有 3 行

## ADDED Requirements

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
