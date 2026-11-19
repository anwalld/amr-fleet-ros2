# Tasks

**验证规则**
- 白天：在 Mac 上用 `ctest` 跑 `robot_console_core` 的单测；推送前在虚拟机上跑 `colcon build/test --packages-select robot_console_core robot_console`（属于单测级别）。
- 全栈验证（全量 colcon test、验收、压测、场景、冒烟）一律交给当晚的 `scripts/nightly.sh`，白天不单独跑。用户明确授权的情况除外。
- 在 Mac 上安装软件、提交、推送、开 PR 之前，都要先问用户。

## 1. 包骨架与环境

- [x] 1.1 在 Mac 上用 Homebrew 安装 Qt（先问用户；用户选了只含基础模块的 `qtbase`，与 `qt6-base-dev` 对应）。验证：`brew --prefix qtbase` 有输出，`"$(brew --prefix qtbase)/bin/qmake" -query QT_VERSION` 显示 6.x。结果：6.12.0；虚拟机上经用户同意装了 `qt6-base-dev` 6.4.2
- [x] 1.2 新建 `src/robot_console_core`：
  - package.xml 用 format 3、`build_type=cmake`，只依赖 `qt6-base-dev`；
  - CMakeLists 写 `find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)`、C++17，产出静态库，并导出 `robot_console_coreConfig.cmake`；
  - ctest 给每个 QtTest 设置 `QT_QPA_PLATFORM=offscreen`；
  - 先放一个空的 QtTest 占位。

  验证：Mac 上按 design 第 1 节的三条命令能完成配置、编译，`ctest` 通过；虚拟机上 `colcon build --packages-select robot_console_core` 通过，`colcon test --packages-select robot_console_core` 能列出这个测试。
- [x] 1.3 新建 `src/robot_console`（ament_cmake）：`find_package(robot_console_core)`、`rclcpp`、两个接口包、`nav_msgs`、`std_msgs`；`main` 先只做 `rclcpp::init_and_remove_ros_arguments` 加一个空窗口。验证：虚拟机上 `colcon build --packages-up-to robot_console` 通过；`rosdep check --from-paths src/robot_console_core src/robot_console --ignore-src` 显示全部满足。

## 2. 数据结构、JSON 解析与回放数据源

- [x] 2.1 在 core 里定义路网、位置、交通状态、快照、事件五个结构体，字段名照抄 ROS 接口，并声明、注册元类型；实现 JSON 到结构体的解析；手写 `test/data/mini.jsonl`，包含 `meta` 和其余每种 kind 各至少一行。验证：QtTest `test_json_codec` 对 mini 文件逐行解析，结果与预期一致，其中 `traffic` 的键和 `TrafficState.msg` 的字段一一对应。
- [x] 2.2 实现回放文件校验：缺 `meta`、`meta.v` 过高时拒绝加载；不认识的 kind 和坏行跳过并计数；不认识的 meta 键忽略。验证：QtTest 覆盖规格"回放文件校验"的两个场景，外加"坏行跳过"。
- [x] 2.3 实现 DataSource 基类，包括六个信号和计数器；实现 ReplaySource，支持按 `t` 播放、暂停、继续、×1、×4、循环（循环时补发第一份路网），时钟可以注入，方便测试。验证：QtTest `test_replay_source` 用注入的时钟验证：60 秒的文件在 ×4 下约 15 秒发完、顺序不变；暂停期间不发数据；循环时第一条发出的是路网。

## 3. 主窗口、状态栏、车队表与任务队列（第 1、2 步）

- [x] 3.1 主窗口接收一个 DataSource 指针；状态栏显示数据源、`mission_state`、执行中的任务号、队列长度、快照距今秒数，超过 3 秒标"快照过期"。验证：QtTest 在 offscreen 下喂一份快照，把时钟拨快 4 秒后，状态栏显示"快照过期"。
- [x] 3.2 车队表模型：列出车辆号、启用、状态、电压（按伏特原样显示）、当前站点、当前任务（取状态不是 FINISHED、FAILED、CANCELED 的远程任务）。验证：QtTest 覆盖规格"车队表"的两个场景。
- [x] 3.3 任务队列表模型：执行中的任务、按快照顺序排列的排队任务（带优先级）、远程任务；每份快照到达时整体刷新。验证：QtTest 覆盖"排队顺序"场景。
- [x] 3.4 写 `robot_console_replay <文件> [--speed 4] [--loop] [--quit-after 秒]`；写 `src/robot_console_core/README.md`，说明 Mac 上怎么编译、怎么跑回放、回放格式在哪里定义。验证：Mac 上 `QT_QPA_PLATFORM=offscreen robot_console_replay test/data/mini.jsonl --quit-after 3` 退出码为 0；按 README 里的命令能从头编译并运行。

## 4. 地图（第 3 步）

- [x] 4.1 资源号解析纯函数：`route_edge:A__B` 解析成无序站点对，`route_node:S` 解析成站点，其他的算无效。验证：QtTest 覆盖路段、站点、无效、两站顺序颠倒四种情况。
- [x] 4.2 地图场景：站点画点并标编号；路段画线，单向带箭头、禁用画虚线；y 轴朝上；没有底图时自动缩放到全部站点。验证：QtTest 从 mini 路网建场景，检查图元数量、禁用路段的画笔是虚线、单向路段有箭头，视图范围包含全部站点。
- [x] 4.3 可选底图：读地图 yaml 和 PGM，按分辨率和原点对齐。验证：QtTest 生成一张小 PGM 和 yaml（`resolution=0.05`、`origin=[-10,-5,0]`），检查站点 `(0,0)` 落在像素列 200、距底边 100 像素处（规格"底图对齐"场景）。
- [x] 4.4 车辆图元按位置和朝向绘制，没收到过位置的车不画；33 ms 合并刷新。验证：QtTest 推一条 `robot_3` 位置 `(2,1,1.5708)`，定时器触发一次后检查图元的位置和旋转角。
- [x] 4.5 地图视图在 `paintEvent` 里记延迟样本，和系统时间相差超过 10 秒的样本计入 `latency_skipped`。验证：QtTest 在 offscreen 下显示视图，推一条 `stamp` 为 50 ms 前的位置后发生了绘制，并记下一个不小于 50 ms 的样本；推一条 20 秒前的位置，`latency_skipped` 加一（同时排除 design 风险里"offscreen 下不绘制"的可能）。

## 5. 交通面板与路段着色（第 5 步）

- [x] 5.1 路段和站点按占用者着色：固定调色板，同一车辆颜色不变；`traffic_block:` 用封锁样式；无法解析的资源号计数；释放后恢复默认样式。验证：QtTest 覆盖规格"路段按占用着色"的两个场景，外加封锁样式和无效资源计数。
- [x] 5.2 等待虚线和死锁车辆突出显示，条件消失后去掉。验证：QtTest 覆盖"三车成环"场景，再喂一条没有等待的交通状态，检查虚线和突出显示都已清除。
- [x] 5.3 交通面板：占用表模型、等待队列模型（同一资源按队内位置排序），死锁信息用标签显示，`last_resolution_ms = -1` 时显示"尚未解开"。验证：QtTest 覆盖"尚未解开"场景和等待队列排序。

## 6. RosBridge 与 robot_console（只读连接）

- [x] 6.1 ROS 消息到结构体的转换函数，覆盖五类数据，其中 odom 的四元数转 yaw、事件的 JSON 解析。验证：gtest 构造各类消息，转换结果与预期一致；虚拟机上 `colcon test --packages-select robot_console` 全绿。
- [x] 6.2 RosBridge 按 design 第 5 节实现：QThread 加执行器；订阅 `/traffic/state`、`/fleet/agent_events`；快照每秒一次，路网每 5 秒重查一次、有变化才重发；每类请求只挂一个，3 秒超时并计数；按快照动态订阅位置话题；支持 `odom_topic_overrides`。超时的记账逻辑写成纯函数。验证：gtest 覆盖超时记账（超时计数、挂起中不重发）；`grep -n "create_publisher\|create_client" src/robot_console/src` 只列出三个服务客户端、没有发布者；连真栈的行为由当晚第 7 阶段验证。
- [x] 6.3 冒烟模式：
  - 在 core 里实现度量记录器：条数、频率、每车频率的最低值、超时次数、延迟的 p50、p95 和最大值、帧数、帧率、单帧绘制耗时 p95、`latency_skipped`；
  - `robot_console` 支持 `--smoke-seconds`、`--smoke-out`、`--smoke-wait-seconds`（默认 120）。

  验证：QtTest 测度量记录器的分位数和频率计算；虚拟机上不起栈，运行 `QT_QPA_PLATFORM=offscreen ros2 run robot_console robot_console --smoke-seconds 5 --smoke-wait-seconds 3 --smoke-out <临时文件>`，退出码为 2，也没有生成文件。

## 7. 采集、裁剪与冒烟脚本

- [x] 7.1 写 `scripts/console_capture.py`，按 design 第 8 节实现：订阅和轮询与 RosBridge 一致；`meta` 由参数给出，`rev` 取 `NIGHTLY_REV`；每秒刷盘；收到 INT 或 TERM 时刷盘并以 0 退出。文件头注释写明用法。验证：`python3 -m py_compile` 通过，`--help` 列出全部参数；实际录制由当晚第 5、6 阶段验证。
- [x] 7.2 在 `run_fleet_traffic_stress.sh` 和 `run_deadlock_scenarios.sh` 里加 `CONSOLE_CAPTURE=1` 开关：拉起栈后在后台采集，日志写 `.capture.log`；清理时先 INT 采集进程、最多等 5 秒；采集的退出码不参与判定；文件名按规格（`console.jsonl` / `scenario_<场景>_<遍次>.console.jsonl`）。验证：`bash -n` 和 `shellcheck` 通过；`git diff` 确认开关关闭时的执行路径没有变化。
- [x] 7.3 写 `scripts/console_replay_trim.py <输入> <输出> --from T0 --to T1`，按 design 第 9 节实现。验证：Mac 上对 `mini.jsonl` 截一段，输出的 `meta` 带有 `trimmed_from` 和 `trimmed_to`，第一条记录是 `t=0` 的路网，窗口内的 `t` 已平移到从 0 开始。
- [x] 7.4 写 `scripts/run_console_smoke.sh <输出目录>`，按 design 第 10 节实现：复用压测脚本拉起 6 车 rolling 栈，前台跑冒烟，判定后写 `console_smoke.tsv`，最后停掉进程组；文件头注释写明用法和判定规则。验证：`bash -n` 和 `shellcheck` 通过；虚拟机上 `DRY_RUN=1 scripts/nightly.sh` 时，第 7 阶段显示"DRY_RUN：冒烟脚本存在"，不再是"包还没有"。
- [x] 7.5 修改 `scripts/nightly.sh`：第 5 阶段只在 6 台 rolling 这一组设置 `CONSOLE_CAPTURE=1`；第 6 阶段整个设置；阶段汇总里列出录像文件的行数和大小。验证：`bash -n` 和 `shellcheck` 通过；`DRY_RUN=1` 时报告正常生成。

## 8. 推送前检查

- [x] 8.1 在虚拟机上（Qt 6.4.2）跑 `colcon build --packages-select robot_console_core robot_console` 和 `colcon test --packages-select robot_console_core robot_console`，全部通过。用到 6.4 没有的 API 的地方在这一步改掉。验证：`colcon test-result --verbose` 中两个包都没有失败。

## 9. 夜跑验收与记录

- [ ] 9.1 第一夜跑完整 `scripts/nightly.sh`（跑前在 Mac 上执行 `caffeinate -i -w <VMware 进程号>`）。验证，以次日报告为准：
  - 阶段 1、2 通过，其中包含新包的测试；
  - 阶段 6 达到 9/9，M3 留下的 `outside_wait` 修复得到验证；阶段 6 每一遍都有 `scenario_*.console.jsonl`，且含 graph、pose、traffic、snapshot 四类记录；
  - 阶段 5 的 6 台 rolling 有 `console.jsonl`；
  - 阶段 7 为 `verdict PASS`，并给出延迟和帧率；
  - 阶段 3 与上一夜逐项 diff，没有新增失败。

  没达标的项修好后并入当晚再跑，不在白天重跑。
- [ ] 9.2 从虚拟机取一遍 ring3 录像，用 `console_replay_trim.py` 截约 60 秒（从成环前 10 秒到解开后 20 秒），提交为 `src/robot_console_core/test/data/ring3.jsonl`（小于 1 MB）；新增 QtTest：整份文件解析下来没有跳过的行，用它回放时交通面板先显示三车成环、之后显示已解开。验证：Mac 上 `ctest` 和虚拟机上 `colcon test --packages-select robot_console_core` 都通过。
- [ ] 9.3 新建 `docs/baseline/M4.md`，记录以下内容，所有数字都引用夜跑报告 `docs/baseline/nightly/<日期>.md`，不手填：
  - 第 7 阶段的度量，注明是在 offscreen 下测的；
  - 第 5 阶段 6 台 rolling 吞吐与 2026-11-18 各夜同组的对比，作为采集干扰的检查。吞吐低于此前最低值时，先关掉第 5 阶段的采集，并记下原因。

  验证：文件里每个数字旁边都注明了出处报告。
- [ ] 9.4 运行 `openspec validate m4-console-core --strict`，通过；更新交接文档的"下一步"和里程碑表。

## Workflow follow-up

- 运行 `/opsx:verify`，对照提案核验实现，并处理它的报告。
- 推送和开 PR 前先问用户。开 PR 后看 CI：确认 rosdep 装上了 `qt6-base-dev`，并且 `colcon test-result` 能看到 core 的 QtTest 结果；看不到的话按 design 的风险条目改成输出 JUnit XML。
- CI 变绿后合并，然后运行 `/opsx:archive`。
- 下一个变更是 `m4-console-ops`。写提案前先去 `src/robot_tasks/src/mission_runner_node.cpp` 查清楚远程任务的暂停、恢复、取消入口。
