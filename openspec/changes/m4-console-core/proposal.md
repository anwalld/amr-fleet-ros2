# Proposal

## Why

M3 之后，调度核心的路权、等待队列和死锁状态都已经在 `/traffic/state` 上发布，但只能用 RViz 标记或命令行看，没有一个能同时看到车队、任务、地图和路权的上位机。M4 要做 Qt 6 调度台（二开计划第 4 节）。本变更是 M4 三个变更中的第一个：只做只读展示，把数据通路、回放和界面骨架搭起来，并让夜跑阶段 7 第一次产出界面刷新延迟和帧率这两个真实数字。

## What Changes

- **新增两个包**：
  - `src/robot_console_core`：纯 CMake 包（`build_type=cmake`），只依赖 Qt 6，不依赖 ROS。包含 DataSource 接口、读 JSONL 的 ReplaySource、模型层、地图场景、主窗口，以及回放程序 `robot_console_replay` 和 QtTest。可以在 Mac 上直接用 `cmake -S` 编译和测试，也可以在虚拟机或 CI 上由 colcon 编译。
  - `src/robot_console`：ament_cmake 包。RosBridge 在 QThread 里跑 rclcpp 执行器，实现 DataSource 接口；可执行程序名为 `robot_console`。
- **只读展示，对应二开计划 4.4 节第 1、2、3、5 步**：
  - 双线程主窗口，状态栏显示快照里的任务状态；
  - 车队表和任务队列表；
  - 地图视图：只画路网，底图可选；画站点、路段，以及车辆位置和朝向；
  - 交通面板：占用表、等待队列、死锁字段；路段按占用车辆着色；等待中的车用虚线指向它在等的资源。
- **回放格式**：JSONL，每行一条记录。`kind` 取 `meta`、`graph`、`pose`、`traffic`、`snapshot`、`event` 之一。字段名和对应的 ROS 消息保持一致，`pose` 是唯一的例外，只保留 x、y、yaw、vx、wz 和原始时间戳。第一行 `meta` 记录这份数据来自哪一次真实运行。
- **夜跑采集**：
  - 新增 `scripts/console_capture.py`（rclpy），按回放格式记录；
  - `run_fleet_traffic_stress.sh` 和 `run_deadlock_scenarios.sh` 增加开关 `CONSOLE_CAPTURE=1`，默认关闭；
  - `nightly.sh` 第 6 阶段全部录，第 5 阶段只录 6 台 rolling 这一组；
  - 压测和场景的 Python 驱动不改；
  - 从三车成环场景截一段约 60 秒的录像，提交进仓库，作为测试数据和 Mac 开发数据。
- **夜跑阶段 7 冒烟**：新增 `scripts/run_console_smoke.sh`。在 offscreen 模式下让 `robot_console` 对着 6 车压测栈跑 5 分钟，输出 `console_smoke.tsv`：各类消息的条数和频率、位置从发布到绘制的延迟、帧率、单帧绘制耗时。阶段 7 从此不再记 SKIP。
- 不改任何现有 ROS 接口，也不改 `GetOperatorSnapshot`；`tools/operator_console.html` 原样保留。

## Capabilities

### New Capabilities

- `operator-console`：Qt 调度台对外承诺的行为，包括数据源接口与回放格式、RosBridge 的线程与订阅方式、车队与任务表、地图与路段着色、交通面板，以及冒烟度量的定义。后续 `m4-console-ops`（写操作）和 `m4-console-alarm`（告警、急停）都在这份规格上修改。

### Modified Capabilities

- `acceptance-validation`：新增两项要求。一是压测和死锁场景脚本可以选择同时采集调度台回放；二是调度台冒烟脚本及其 `console_smoke.tsv` 输出和判定规则。

## Impact

- **代码**：新增 `src/robot_console_core`、`src/robot_console`。新增 `scripts/console_capture.py`、`scripts/console_replay_trim.py`、`scripts/run_console_smoke.sh`。修改 `scripts/run_fleet_traffic_stress.sh`、`scripts/run_deadlock_scenarios.sh`（只加采集开关）和 `scripts/nightly.sh`（第 5、6 阶段设置开关）。
- **依赖**：仓库第一次引入 Qt 6。rosdep 键 `qt6-base-dev` 写进两个包的 package.xml，CI 会自动安装；虚拟机和 CI 上是 Qt 6.4.2。Mac 上用 Homebrew 的 Qt（目前是 6.12），CMake 只要求 6.4 及以上。是否用到了 6.4 没有的 API，由虚拟机白天单测编译和 CI 拦截。
- **接口**：无变化。RosBridge 只订阅话题，只调用 `/v2/list_stations`、`/v2/list_station_routes`、`/v2/get_operator_snapshot` 这三个只读服务。
- **验证**：白天在 Mac 上用 `ctest` 跑 core 的单测，推送前在虚拟机上跑 `colcon build/test --packages-select robot_console_core robot_console`。录像、冒烟、6 台 rolling 吞吐和前几夜的对比，全部来自夜跑报告。M3 留下的 `outside_wait` 场景修复也在第一夜一并验证。
- **不在范围内**：
  - 下单、任务控制、封路和解封、启停车、清除占用：留给 `m4-console-ops`，远程任务控制入口也在那时去查；
  - 事件与告警面板、急停（包括车队急停后端）、断连重连界面、打包：留给 `m4-console-alarm`；
  - 虚拟机桌面，以及在桌面上连真栈的那次验证：M4 收尾时和 M5 录视频一起做；
  - 车间路网的底图：目前没有底图文件。
