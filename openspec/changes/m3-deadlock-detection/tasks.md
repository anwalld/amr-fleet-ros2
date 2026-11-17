# Tasks

白天只在虚拟机上跑改动包的单测（`colcon test --packages-select <包>`）；全栈验证（全量 colcon test、验收、压测、场景）一律交给当晚的 `scripts/nightly.sh`，不白天单独跑。

## 1. 接口追加

- [x] 1.1 `RequestRouteSegment.srv` 请求末尾追加 `string goal_station`；`TrafficState.msg` 末尾追加 design 决定 8 列出的死锁字段；新增 `TrafficYield.msg`（车辆号、动作、环上车辆、应绕开的资源、路径、停留秒数）并登记到 `robot_interfaces_mission/CMakeLists.txt`。验证：虚拟机上 `colcon build --packages-up-to robot_interfaces_mission` 通过，`ros2 interface show robot_interfaces_mission/msg/TrafficYield` 输出正确字段

## 2. 统一占用者与资源号（隐患一、二）

- [x] 2.1 whole_route 下站点运输入队锁路改用执行车辆的车辆号作为占用者；`route_locks_by_mission` 仍按任务号记录，释放时只放掉不被同车其他未结束工单引用的资源；解封失败消息改为 `route resource is reserved by: <占用者>`。验证：`traffic_reservation_test.cpp` 新增"同车两单共用资源，第一单结束后资源仍被占"与"占用者为车辆号"用例并通过
- [x] 2.2 车队代理改为按授予应答 / 授予通知里的资源号释放；封路集合与绕行集合改存无序站点对（由资源号解析），删除代理里的 `RouteEdgeLockId`/`RouteNodeLockId` 拼接。验证：`grep -n 'route_edge:\|route_node:' src/robot_simulation/src/fleet_robot_agent_node.cpp` 只剩解析用的前缀常量；`colcon build --packages-select robot_simulation` 通过
- [x] 2.3 `scripts/fleet_traffic_stress.py` 删除 `edge_id()`，改为从 `/traffic/state` 占用表解析"车 → 无序站点对"后判定"车在路段上是否持有该路段"。验证：`python3 -m py_compile scripts/fleet_traffic_stress.py` 通过；全栈行为由当晚阶段 5 验证（违规数为 0）
- [x] 2.4 更新 `docs/baseline/M3.md`（新建）记录这两处隐患的处理方式和对外影响（任务号不再作为占用者）

## 3. 防饿死（隐患三）

- [x] 3.1 在补授予纯函数里加入暂扣：等待超过 `traffic_wait_starvation_s` 的等待者，其所需空闲资源不授予排在它后面、优先级不高于它的车；暂扣表作为输出供等待图使用，不写入占用表。验证：新增 GoogleTest"拿不齐的等待者被跳过（未超时）"与"等待过久不再被插队"，通过
- [x] 3.2 `TrafficService` 声明参数 `traffic_wait_starvation_s`（默认 20），在释放、解封、清除预约与定时兜底处传入当前时间。验证：`colcon test --packages-select robot_tasks` 全绿

## 4. 等待图检测（纯函数）

- [x] 4.1 实现等待图构建与找环纯函数：输入占用表、等待队列、whole_route 重试等待表、暂扣表，输出互不相交的环（车辆序列 + 边上资源），三色 DFS。验证：GoogleTest 覆盖无环、两车对向、三车成环、环外有车等待、一组资源被两车挡住、重试等待过期后边消失，全部通过
- [x] 4.2 用新检测替换 `PlanTrafficDeadlockDetection` 的实现，按 design 决定 9 填 `DetectTrafficDeadlock` 应答字段。验证：GoogleTest"两车互等 → deadlocked=true 且字段正确"与"只共用站点 → deadlocked=false"通过；原冲突候选测试按新语义改写

## 5. 解除（纯函数）

- [x] 5.1 实现选让路车：优先级最低 → 剩余最短路程最长 → 车辆号最大。验证：GoogleTest 覆盖三级比较各一例
- [x] 5.2 实现改道规划：去掉环上他车占用资源与封路后求让路车到 `goal_station` 的最短路。验证：GoogleTest"改道成功"（有绕行）与"无绕行返回空"
- [x] 5.3 实现退避规划：选最近的空闲避让点（`yield_point: true`，未标注时 `park_` 前缀）；站点文件解析支持 `yield_point` 字段。验证：GoogleTest"改不了道退避"与"无空闲避让点 → 兜底"
- [x] 5.4 实现兜底计数：按环上车辆集合记录成环时间，30 秒内第三次返回 `pause`；暂停的车等待冻结，清除其占用后解冻。验证：GoogleTest"30 秒三次成环兜底"与"超过 30 秒不累计"
- [x] 5.5 实现解除耗时计算（检出到该组车不再成环，单调时钟）与单次检测耗时统计。验证：GoogleTest 用注入的时间验证耗时计算

## 6. 任务中枢接线

- [x] 6.1 `TrafficService`：申请新进入等待（含 whole_route 被拒记重试等待，TTL 参数 `traffic_whole_route_wait_ttl_s` 默认 6）时检测一次；1 Hz 定时器全量检测；参数 `traffic_deadlock_resolution_enabled`（默认 true）、`traffic_yield_hold_s`（默认 5）；解除时撤掉让路车等待并在 `/traffic/yield` 发指令；`/traffic/state` 填死锁字段；申请服务读取 `goal_station`。验证：`colcon test --packages-select robot_tasks` 全绿；日志中检测、解除各有一条可 grep 的行
- [x] 6.2 把以上参数写进 `fleet_sim.launch.py` 的透传参数（含 `traffic_deadlock_resolution_enabled`），`network` 增加 `workshop_2way`。验证：虚拟机上 `ros2 launch robot_bringup fleet_sim.launch.py --show-args` 列出新参数

## 7. 路网与车队代理

- [x] 7.1 新增 `src/robot_tasks/config/stations/workshop_2way_stations.yaml`：沿用 `workshop` 站点与停车位，横穿段与外环两段改为双向，文件头注释写明可能出现的对向与成环位置。验证：站点间距 ≥ 3 米（加载时打印最小间距），任务中枢以该路网启动无报错（由当晚阶段 5、6 验证）
- [x] 7.2 代理每次申请带 `goal_station`；订阅 `/traffic/yield` 执行 `reroute`（临时绕行集合）、`backoff`（去避让点、停留、再重新寻路）、`pause`（停止申请直到持有路权被清除），本程结束清空绕行集合。验证：`colcon build --packages-select robot_simulation` 通过；行为由当晚阶段 6 验证

## 8. 压测与场景脚本

- [x] 8.1 直接修改 `scripts/run_fleet_traffic_stress.sh`（不另写压测脚本）：增加环境变量 `NETWORK`（默认 workshop）、`DEADLOCK_RESOLUTION`（默认 true）透传给启动文件，结果目录写明二者；它调用的 `fleet_traffic_stress.py` 加死锁指标列：`state.csv` 加累计检出数与 reroute/backoff/pause 累计数，新增 `deadlocks.csv`（时间、环上车辆、让路车、动作、解除耗时毫秒，逐条来自 `/traffic/yield` 与 `/traffic/state`），`summary.json` 加检出次数、各解除方式次数、解除耗时均值/最大、单次检测耗时均值/最大、`network`，键名与 `scripts/nightly.sh` 阶段 5 的 `metrics.tsv` 取值一致：`deadlocks_detected`、`deadlock_reroute`、`deadlock_backoff`、`deadlock_pause`、`resolution_ms_mean`、`resolution_ms_max`、`detection_us_mean`、`detection_us_max`。验证：`bash -n`、`shellcheck`（如有）、`python3 -m py_compile` 通过；指标由当晚阶段 5 产出
- [x] 8.2 新增 `scripts/run_deadlock_scenarios.sh <输出目录> [遍数]`：两车对向、三车成环、环外有车等待各跑 N 遍（默认 3），每遍整栈重启，所有 `ros2 service call` 带 `timeout`，输出 `deadlock_scenarios.tsv`（场景、遍次、是否检出、解除动作、解除耗时毫秒、结论），全部 PASS 退出 0。验证：`bash -n` 与 `shellcheck` 通过；结果由当晚阶段 6 产出
- [x] 8.3 按新语义改写 `scripts/check_traffic_deadlock_detection.sh`：关闭解除后构造两车互等判 `deadlocked=true`，再判只共用站点为 `false`；所有服务调用带 `timeout`。验证：`bash -n` 通过；当晚阶段 3 中 `safety:traffic_deadlock` 为 PASS
- [x] 8.4 `scripts/nightly.sh` 阶段 5 改为以 `NETWORK=workshop_2way` 跑 2/4/6 × whole_route/rolling 矩阵并汇总 `metrics.tsv`（含死锁列），阶段 6 调用场景脚本，不再 SKIP。验证：`bash -n` 与 `shellcheck` 通过；在虚拟机上 `DRY_RUN=1` 时阶段 5、6 不再显示 SKIP

## 9. 夜跑验收与记录

- [ ] 9.1 第一夜：`scripts/nightly.sh` 全阶段跑一遍（跑前在 Mac 上执行 `caffeinate -i -w <VMware 进程号>`）。验证：次日报告中阶段 1、2 通过；阶段 6 三个场景各 3/3 PASS；阶段 5 矩阵 6 组（2/4/6 × whole_route/rolling）都有 `summary.json`；阶段 3 与上一夜逐项 diff 无新增失败（`safety:traffic_deadlock` 为 PASS）。未达标项修复后并入当晚再跑，不白天重跑
- [ ] 9.2 把阶段 5 对照数据（吞吐、平均等路权、死锁检出次数、解除方式与耗时、单次检测耗时）和阶段 6 结果写进 `docs/baseline/M3.md`，数字全部引用夜跑报告 `docs/baseline/nightly/<日期>.md`，不手填
- [ ] 9.3 运行 `openspec validate m3-deadlock-detection --strict` 通过；更新交接文档的"下一步"和里程碑表

## Workflow follow-up

- `/opsx:verify` 对照提案核验实现，处理其报告。
- `gh pr create` 开 PR，等 GitHub CI 绿再合并（推送与开 PR 前先问用户）。
- 合并后 `/opsx:archive`，确认 `route-reservation`、`fleet-robot-agent`、`acceptance-validation` 三份主规格已更新。
