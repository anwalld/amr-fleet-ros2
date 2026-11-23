# Spec Delta

## ADDED Requirements

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
