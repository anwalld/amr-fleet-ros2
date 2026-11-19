# Design

## Context

动机和范围见 proposal.md，对外行为见 `specs/operator-console/spec.md` 和 `specs/acceptance-validation/spec.md`。基线是 `main` @ `714ae6e`（M3 已合并）。下面是和本设计有关的现状。

**数据源**
- `/traffic/state`（`TrafficState.msg`，1 Hz）含占用表、等待队列和 M3 的死锁字段。资源号格式由 route-reservation 规格规定：`route_edge:A__B`（两站按字典序排）和 `route_node:S`。
- `/v2/get_operator_snapshot` 返回任务、队列、车队、远程任务和最近事件，不含车辆坐标和路权。
- `/v2/list_stations` 给出站点的 x、y、yaw（地图坐标系）。`/v2/list_station_routes` 给出有向路段、`bidirectional` 和 `enabled`。
- 轻量仿真里每辆车由代理节点以 10 Hz 发 `/<id>/odom`，消息头时间戳取节点的 `now()`。`fleet_sim` 不用仿真时间，所以这个时间戳就是系统时间。Gazebo 栈里 1 号车发的是 `/odom`。
- `/fleet/agent_events` 是 `std_msgs/String`，内容是 JSON。目前只有"任务完成"这一种事件。
- `amr_sim_visualizer_node` 已经在轮询快照、解析 `route_edge:`，但它是 RViz 标记节点，依赖 ROS，所以这里只作参考，不复用。

**环境**
- 车间路网没有底图文件。
- Mac 上没有 ROS，虚拟机上没有桌面。Mac 用 Homebrew 的 `qtbase` 6.12（只含基础模块），虚拟机和 CI 用 rosdep 键 `qt6-base-dev` 装的 6.4.2。
- 仓库现有 19 个包全是 ament_cmake，没有用 Qt 的。

**夜跑**
- `scripts/nightly.sh` 里已经留好了第 7 阶段的入口：`src/robot_console` 目录存在、`scripts/run_console_smoke.sh` 可执行时，用 `QT_QPA_PLATFORM=offscreen` 调用它，并要求阶段目录下产出 `console_smoke.tsv`。
- 第 5 阶段 6 台 rolling 的吞吐在 2026-11-18 的几轮夜跑里分别是 66、116、122、122 单/小时（见 `docs/baseline/nightly/2026-11-18*.md`），各轮之间波动很大。

## Goals / Non-Goals

**Goals:**
- Mac 上编译的 core 和 CI 编译的是同一个包、同一个构建入口。
- 回放格式只有一个生产者（采集脚本），模型层和地图场景都能用 QtTest 在 offscreen 下测试。
- 冒烟度量的口径一次定清楚，第 7 阶段第一次运行就能给出可以引用的数字。

**Non-Goals:**
- 不用 QML。按二开计划用 Widgets，也不引入 `qt6-base-dev` 以外的 Qt 模块，比如 Charts。
- 回放不支持拖动定位，不做录制端以外的编辑。
- 除了合并刷新之外不做性能优化，等第 7 阶段的数字出来再说。
- 断连只计数、在状态栏显示快照过期，不做重连界面（属于 `m4-console-alarm`）。

## Decisions

### 1. 两个包，边界由链接关系保证

`robot_console_core`：
- package.xml 用 format 3、`build_type=cmake`，依赖只有 `qt6-base-dev`。
- CMake 产出静态库 `robot_console_core`、可执行程序 `robot_console_replay`，以及 ctest 跑的 QtTest。
- 安装头文件，并用 `install(EXPORT ...)` 导出 `robot_console_coreConfig.cmake`，供 `robot_console` 使用 `find_package`。

`robot_console`：
- ament_cmake 包，依赖 `robot_console_core`、`rclcpp`、`nav_msgs`、`std_msgs`、`robot_interfaces_mission`、`robot_interfaces_business`、`qt6-base-dev`。
- 产出 `robot_console` 可执行程序，以及 RosBridge 转换函数的 gtest。

core 的任何目标都不链接 ROS，所以误加 `#include <rclcpp/...>` 在 Mac 和虚拟机上都会编译失败。

为什么用静态库：colcon 的安装目录之间不需要处理动态库的 rpath，而且 core 只被 `robot_console` 一个包使用。

备选方案：一个包，CMake 里用 `find_package(ament_cmake QUIET)` 分两条路径。否决，原因是 Mac 走的那条路径 CI 永远覆盖不到（探讨时已定）。

Mac 上的构建目录放在仓库外面，避免同步到虚拟机：

```
cmake -S src/robot_console_core -B ~/build/robot_console_core -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build ~/build/robot_console_core -j
ctest --test-dir ~/build/robot_console_core --output-on-failure
```

### 2. Qt 版本门槛

- CMake 写 `find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)`，C++17。
- 只用 Core、Gui、Widgets、Test 四个模块，JSON 用 Core 里的 `QJsonDocument`。
- `find_package` 的版本号只限制最低版本，拦不住用到 6.5 之后才有的 API，所以还有两道把关：推送前在虚拟机上跑 `colcon build/test --packages-select robot_console_core robot_console`（属于单测级别，白天可以跑），以及 CI。
- 不靠宏来限制 API 版本，因为 Qt 没有提供"禁止使用新 API"的开关。

### 3. 数据结构与 ROS 字段同名，两个转换器各自测试

core 里为五类数据各定义一个普通结构体：路网、位置、交通状态、快照、事件。
- 字段名逐个照抄 ROS 接口，类型用 Qt 类型（`QString`、`QVector`），方便界面直接使用。
- 结构体用 `Q_DECLARE_METATYPE` 声明，并在启动时注册，供跨线程的排队信号传递。

转换器有两个：
- core 里是 JSON 到结构体，用 ring3 的真实录像和手写的小数据测试；
- `robot_console` 里是 ROS 消息到结构体，用 gtest 构造消息来测试。

两边按同一张字段表逐字段照抄，没有再加一层映射。

备选方案：回放文件存精简后的结构体，字段名另起。否决，因为那样采集脚本就得用 Python 再实现一遍 C++ 的精简逻辑，两边容易对不上。

DataSource 是一个 `QObject` 基类，信号对应六类数据：路网、位置、交通状态、快照、事件、连接状态。另外提供计数器，包括服务超时次数和回放跳过的行数。

### 4. 回放数据源

- 加载时把整个文件读进内存：60 秒的片段约 0.3 MB，30 分钟的录像约 20 MB，都没有问题。
- 读入后先校验 `meta`，不认识的 meta 键直接忽略，以便以后加字段时向前兼容。
- 用 `QElapsedTimer` 加一个 10 ms 的 `QTimer` 驱动播放：每次触发时，把 `t <= 已播时间 × 倍速` 的记录全部发出。暂停时冻结已播时间。
- 循环播放回到开头时，先补发第一份 `graph`。
- `robot_console_replay <文件> [--speed 4] [--loop]` 打开主窗口。主窗口和 `robot_console` 用的是同一个类，只是数据源不同。

### 5. RosBridge 的线程模型

**线程与生命周期**
- worker 对象移到一个 `QThread` 里，在线程里创建 `rclcpp::Node` 和 `SingleThreadedExecutor`，然后 `spin()`。
- 退出时调用 `executor.cancel()`，再结束线程。
- `main` 用 `rclcpp::init_and_remove_ros_arguments` 先把 ROS 参数取走，剩下的交给 `QApplication`。

**信号**
- 回调里先转成结构体再 `emit`。接收方在界面线程里，Qt 的 AutoConnection 会自动用排队方式投递，界面代码不碰任何 ROS 对象。

**服务调用**
- 快照每秒一次，路网每 5 秒一次，都用 `async_send_request`。
- 每类请求同时最多挂一个，一个 500 ms 的检查定时器把超过 3 秒的请求记为超时并丢弃，下一轮照常发，避免请求越堆越多。

**话题与 QoS**
- 里程计用 `SensorDataQoS`（best effort），它能接上 reliable 和 best effort 两种发布者。
- `/traffic/state` 用默认的 reliable 深度 10，`/fleet/agent_events` 用 reliable 深度 100，和发布端保持一致。

**位置话题的订阅**
- 每份快照到达时，对比 `fleet_robot_ids`，给新出现的车辆在执行器线程里创建订阅。
- 话题覆盖用参数 `odom_topic_overrides`，是一个字符串列表，形如 `robot_1=/odom`。

### 6. 地图场景

**坐标与缩放**
- 场景单位是米。地图坐标 (x, y) 放在场景 (x, -y)，屏幕上 y 轴朝上；视图本身不翻转，这样文字不会被镜像，车辆朝向换算成 `-yaw`。
- 站点编号和车辆图形设置 `ItemIgnoresTransformations`，缩放时保持像素大小不变。

**图元**
- 站点：圆点加编号。
- 路段：线段；单向路段带箭头，禁用路段画虚线。
- 车辆：按 yaw 旋转的三角形，加车辆号。
- 等待：一条虚线。
- 底图：可选，用 `QGraphicsPixmapItem` 读 yaml 指向的图像（`QImage` 原生支持 PGM），按"像素乘分辨率、y 翻转、加原点"摆放。

**资源号解析**
- 写成纯函数：`route_edge:A__B` 解析成无序站点对，`route_node:S` 解析成站点，其他的算无效。
- 一个无序站点对对应一条或两条有向路段，着色时一起处理。

**颜色**
- 固定 10 色调色板。`robot_N` 取第 `(N-1) mod 10` 色，其他车辆号按哈希取色。整个会话内颜色不变。
- `traffic_block:` 开头的占用者统一用深灰色点划线（线条没法画斜线填充）。

**合并刷新**
- 数据到达时只标记"脏"，一个 33 ms 的定时器统一把待更新的位置写进图元并触发重绘，所以帧率上限是 30。
- 地图视图重写 `paintEvent`：绘制完成后，对这一帧里画出的位置有更新的车辆，各记一个延迟样本（系统时间减 `stamp`）。
- 如果某条位置和系统时间相差超过 10 秒，例如 Gazebo 栈用了仿真时间，就不记样本，只计入 `latency_skipped`。

### 7. 模型层

- 车队表、任务队列表、占用表、等待队列表各是一个 `QAbstractTableModel` 子类。
- 每份新快照或新交通状态到达时整体重置（`beginResetModel`）。
- 行数只有几十行，刷新频率 1 Hz，整体重置的开销可以忽略。
- 选中状态会丢失，等到 `m4-console-ops` 需要"选中一行再操作"时再改成增量更新。
- 死锁信息不是表格，用一组标签显示。

### 8. 采集脚本与夜跑挂接

**`scripts/console_capture.py`（rclpy）**
- 订阅、轮询的对象和频率都和 RosBridge 一致（第 5 节），话题覆盖参数也一样。
- `traffic`、`snapshot`、`graph` 用 `rosidl_runtime_py.message_to_ordereddict` 原样导出；`pose` 按规格精简；`t` 取 `time.monotonic()` 减去起始时间。
- `meta` 由命令行参数给出：`--network`、`--robots`、`--mode`、`--source`；`rev` 取环境变量 `NIGHTLY_REV`。
- 每秒刷一次盘。收到 SIGINT 或 SIGTERM 时刷盘并以退出码 0 退出。

**两个 shell 包装脚本**
- 拉起栈之后，如果 `CONSOLE_CAPTURE=1`，就在后台启动采集，日志写到同名的 `.capture.log`。
- trap 清理时先向采集进程发 INT，最多等 5 秒，再停栈。
- 采集进程的退出码不参与判定。

**`nightly.sh`**
- 第 5 阶段只在 `robots=6 mode=rolling` 这一组设置 `CONSOLE_CAPTURE=1`；第 6 阶段整个设置。
- 阶段汇总里列出各录像文件的行数和大小，在报告里就能看到确实采集到了数据。

### 9. 提交进仓库的片段

`scripts/console_replay_trim.py <输入> <输出> --from T0 --to T1`：
- 保留 `meta`，追加 `trimmed_from`、`trimmed_to` 两个字段；
- 取 T0 之前最近的一份 `graph`，记为 `t=0`；
- 窗口内的记录把 `t` 平移到从 0 开始。

窗口取法：第一次出现非空 `deadlock_robot_ids` 前 10 秒，到 `resolved_count` 增加后 20 秒，约 60 秒。截出的文件提交为 `src/robot_console_core/test/data/ring3.jsonl`，只有回放格式的版本升级时才重录。

### 10. 冒烟脚本

`scripts/run_console_smoke.sh <输出目录>`：
- 用 `setsid` 在后台调用 `NETWORK=workshop_2way run_fleet_traffic_stress.sh 6 <分钟> rolling <输出目录>/stack`。分钟数取 `SMOKE_SECONDS/60` 向上取整再加 3，默认 `SMOKE_SECONDS=300`。
- 前台运行 `timeout $((SMOKE_SECONDS+180)) ros2 run robot_console robot_console --smoke-seconds ... --smoke-out <输出目录>/console_metrics.tsv`。
- 按规格判定，把度量和 `verdict` 写进 `console_smoke.tsv`，然后向压测的进程组先发 INT、再发 KILL。压测脚本自己的 trap 会停掉 launch。
- `QT_QPA_PLATFORM` 默认设为 offscreen。

为什么复用压测脚本、不自己写 launch：
- 负载和第 5 阶段同源，订单生成器也一样；
- 压测自带的安全判定（`violations.csv`）可以顺便作为旁证；
- 不需要再维护一份启动逻辑。

### 11. 夜跑节奏

**第一夜**（代码全部写完之后）
- 第 5 阶段 6 台 rolling 录像、第 6 阶段全部录像、第 7 阶段冒烟；
- 同时验证 M3 留下的 `outside_wait` 修复，要求阶段 6 达到 9/9。

**第一夜之后**
- 从 ring3 录像截出片段并提交，再补上依赖它的单测。

**第二夜**
- 如果第一夜有问题，修复后在第二夜再跑。

**采集干扰的检查**
- 第 5 阶段 6 台 rolling 的吞吐和前几夜同组对比。因为各夜本来就在 66 到 122 单/小时之间波动，这个对比只能发现明显的干扰：如果低于此前的最低值，就先关掉第 5 阶段的采集，再去查原因。
- 结论写进 `docs/baseline/M4.md`。

## Risks / Trade-offs

- [offscreen 平台下不发生绘制，或帧率没有意义] → 第一批单测里就加一条"offscreen 下显示地图视图，更新位置后 `paintEvent` 被调用"。报告里注明帧率是 offscreen 下测的，桌面上的数字留到 M4 收尾那次桌面验证。
- [Mac 的 Qt 6.12 和 6.4.2 之间 API 有差别] → 推送前在虚拟机编译，再加 CI 两道把关。只用 base 模块，降低踩到新 API 的概率。
- [Python 采集和 C++ 解析对不上] → 字段名照抄 ROS 接口。core 的解析测试用真实录像，而不是手写的 JSON。
- [采集占用资源，影响第 5 阶段吞吐] → 只录一组，并做第 11 节的对比。这个检查本身比较弱，报告里会写明。
- [延迟里包含合并刷新带来的最多 33 ms] → 这是口径的一部分，规格和报告都写明"从发布到画出"，包含这段时间。
- [CI 安装 Qt 多花时间] → `qt6-base-dev` 只有一次安装开销，可以接受。
- [`colcon test-result` 识别不了纯 CMake 包的 ctest 结果] → 第一次推送后看 CI 日志确认。识别不了的话，改让 QtTest 输出 JUnit XML（`-o file,junitxml`）。

## Migration Plan

两个包都是新增的，不改现有接口。压测和场景脚本新增的开关默认关闭，原有调用不受影响。回滚只需要回退这个提交，再去掉 `nightly.sh` 里设置开关的那两处。

## Open Questions

- 调色板的具体色值，以及车辆图形的大小，按回放里的实际效果再调，不影响规格。
- `robot_console_replay` 要不要也支持冒烟模式，用来在 Mac 上看绘制耗时：做第 3 步时如果觉得有用再加，不进本变更的验收。
