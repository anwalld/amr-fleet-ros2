# Spec Delta

## Purpose

定义项目自带的整栈验证工具对外承诺的行为：逐用例隔离运行全部验收用例并产出可逐项对比的结果表、在验收中代替人工完成站点运输装卸确认、以及多车路权压测的安全判定与输出，使每个里程碑的结论都来自可复现的真实运行。

## ADDED Requirements

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
`scripts/run_fleet_traffic_stress.sh <车数> <分钟> <模式> [输出目录]` SHALL 拉起多车轻量仿真，用固定随机种子在工作站之间生成同一批订单，保持未完成订单数不超过车数，经 `/v2/submit_order`（`fleet_station`）提交，被拒就稍后重试。

#### Scenario: 同参数重跑订单序列相同
- **WHEN** 以相同车数、模式和默认种子运行两次
- **THEN** 两次生成的订单（取货站、卸货站及其顺序）相同

### Requirement: 压测安全判定
压测工具 SHALL 以 10 Hz 检查并把以下情况记为违规：同一资源在交通状态中出现两个占用者；同一路段上两车距离不超过 0.5 m；车在某路段上却持续 1.5 秒以上不持有该路段。另 SHALL 记录任意两车的最小距离。

#### Scenario: 同一路段两车过近
- **WHEN** 两台车同时位于同一路段且相距 0.4 m
- **THEN** 记一条 `same_edge_distance` 违规

#### Scenario: 刚授予就出发不算违规
- **WHEN** 车在被授予路段后立即驶入，而 1 Hz 的交通状态在 1.5 秒内才反映这次占用
- **THEN** 不记违规

### Requirement: 压测输出与退出码
压测 SHALL 在输出目录写出 `state.csv`（每秒一行：占用数、排队数、各车位置）、`tasks.csv`（任务完成明细）、`violations.csv`、`summary.json`（完成任务数、吞吐、等路权时间、违规数及分类、最小距离等）和启动日志。退出码 SHALL 为 0（无违规）、1（有违规）或 2（车队未就绪）。

#### Scenario: 无违规时退出码为 0
- **WHEN** 一轮压测结束且 `violations.csv` 为空
- **THEN** 脚本退出码为 0，`summary.json` 中 `violations` 为 0
