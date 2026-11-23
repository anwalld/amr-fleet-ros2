# Spec Delta

## Purpose

定义 Qt 调度台对外承诺的行为：数据从哪里来，包括真实 ROS 系统和录好的回放文件；回放文件的格式；车队、任务、地图和路权分别怎样展示；以及冒烟模式下界面刷新延迟和帧率怎样度量，保证这些数字来自真实运行。

## ADDED Requirements

### Requirement: 界面只依赖统一的数据源
调度台的界面与模型层 SHALL 只通过统一的数据源接收数据，数据共六类：路网、车辆位置、交通状态、运营快照、车队事件、连接状态。数据源有两种实现：读取回放文件，和连接真实 ROS 系统。界面与模型层 SHALL NOT 直接访问任何 ROS 对象，并且 SHALL 能在没有安装 ROS 的机器上单独编译、运行和测试。

#### Scenario: 无 ROS 的机器上编译和测试
- **WHEN** 在只装了 Qt 6、没有 ROS 的机器上，单独编译调度台的界面与模型层，并运行其单元测试
- **THEN** 编译成功，单元测试全部通过，回放程序能打开回放文件并显示界面

### Requirement: 回放文件格式
回放文件 SHALL 为 UTF-8 的 JSONL，每行一个 JSON 对象，都含 `t`（秒，从采集开始计的单调时钟，非递减）和 `kind`。第一行 SHALL 是 `kind=meta`，含格式版本 `v`（当前为 1）、`network`、`robots`、`mode`、`rev`、`source`（来源运行）。`graph` 含 `stations` 与 `routes`；`traffic`、`snapshot` 的 `data` 是完整消息；`event` 的 `data` 是车队事件解析后的对象。这些内容的字段名 SHALL 与对应 ROS 接口一致。`pose` 只含 `robot`、`stamp`、`x`、`y`、`yaw`、`vx`、`wz`。

#### Scenario: 字段名与 ROS 接口一致
- **WHEN** 读取一条 `kind=traffic` 的记录
- **THEN** `data` 中的键与 `/traffic/state` 消息的字段名一一对应，例如 `resource_ids`、`owner_ids`、`deadlock_robot_ids`

#### Scenario: pose 保留原始时间戳
- **WHEN** 采集到一条车辆里程计消息
- **THEN** 对应的 `pose` 记录中，`stamp` 等于该消息头的时间戳（秒），`t` 等于采集端收到该消息的单调时钟时间

### Requirement: 回放文件校验
加载回放文件时，第一行不是 `meta`、或 `meta.v` 高于调度台支持的版本时，SHALL 拒绝加载并给出原因。遇到不认识的 `kind` 或无法解析的行时，SHALL 跳过该行并计数，SHALL NOT 中止回放。

#### Scenario: 缺少 meta 头
- **WHEN** 加载一个第一行是 `pose` 记录的文件
- **THEN** 拒绝加载，错误信息指出缺少 `meta` 头

#### Scenario: 未知 kind 被跳过
- **WHEN** 文件中有一行 `kind=estop`，而当前版本不认识这个 kind
- **THEN** 该行被跳过、跳过计数加一，其余记录照常回放

### Requirement: 回放播放控制
回放数据源 SHALL 按记录的 `t` 节奏依次发出数据，支持暂停、继续、1 倍速、4 倍速和循环播放。循环播放回到开头时，SHALL 先发出文件中的第一份路网，再继续发出后面的记录。

#### Scenario: 4 倍速播放
- **WHEN** 以 4 倍速播放一个时长 60 秒的回放文件
- **THEN** 约 15 秒后发出最后一条记录，各记录发出的先后顺序与文件中一致

#### Scenario: 暂停
- **WHEN** 播放中按下暂停
- **THEN** 不再发出新记录，直到继续播放；继续后从暂停处接着播放

### Requirement: ROS 连接的线程模型
连接真实系统时，ROS 执行器 SHALL 运行在界面线程以外的独立线程里，消息转换成普通数据后，经排队方式交给界面线程。所有服务调用 SHALL 异步进行，超时 3 秒；超时 SHALL 计数，SHALL NOT 阻塞话题数据的接收和界面刷新。

#### Scenario: 快照服务不应答
- **WHEN** `/v2/get_operator_snapshot` 持续不应答，而里程计话题正常发布
- **THEN** 每次调用在 3 秒后记为超时，地图上的车辆位置照常更新

### Requirement: ROS 连接只读
连接真实系统时，调度台 SHALL 订阅 `/traffic/state` 与 `/fleet/agent_events`；SHALL 每秒调用一次 `/v2/get_operator_snapshot`；SHALL 在启动时调用 `/v2/list_stations` 与 `/v2/list_station_routes`，此后每 5 秒重查一次，结果有变化就重新发出路网。除这三个服务外，SHALL NOT 调用其他服务，SHALL NOT 发布任何话题。

#### Scenario: 封路后地图更新
- **WHEN** 运行期间有人通过 `/v2/block_station_route` 封了一条路段
- **THEN** 5 秒内，地图上该路段显示为禁用

#### Scenario: 不改变系统状态
- **WHEN** 调度台连接真实系统运行 5 分钟
- **THEN** 调度台在此期间没有调用上述三个服务以外的任何服务，也没有发布任何话题

### Requirement: 车辆位置话题
调度台 SHALL 以运营快照里的 `fleet_robot_ids` 作为车辆列表，默认订阅 `/<车辆号>/odom`。SHALL 支持按车辆号指定其他话题，例如 Gazebo 栈中 1 号车的 `/odom`。快照中出现新车辆时 SHALL 自动订阅其位置话题。

#### Scenario: 轻量仿真 6 台车
- **WHEN** 连接 6 车轻量仿真，快照中 `fleet_robot_ids` 为 `robot_1`～`robot_6`
- **THEN** 调度台订阅 `/robot_1/odom`～`/robot_6/odom`，6 台车都出现在地图上

#### Scenario: 按车辆号覆盖话题
- **WHEN** 启动时指定 `robot_1` 使用 `/odom`
- **THEN** 1 号车的位置来自 `/odom`，其余车辆仍用默认话题

### Requirement: 车队表
车队表 SHALL 每辆车一行，列出车辆号、是否启用、状态、电池电压、当前站点、当前任务。电池电压 SHALL 按快照中的伏特数显示，SHALL NOT 换算成百分比。当前任务取分配给该车、状态不是 `FINISHED`、`FAILED`、`CANCELED` 的远程任务；没有这样的任务时显示为空。

#### Scenario: 执行中的车
- **WHEN** 快照中 `robot_2` 的状态是 `busy`，远程任务 `t1` 分配给 `robot_2`、状态为 `RUNNING`
- **THEN** 车队表中 `robot_2` 一行的状态是 `busy`、当前任务是 `t1`

#### Scenario: 任务完成后清空
- **WHEN** 下一份快照中 `t1` 的状态变为 `FINISHED`
- **THEN** `robot_2` 一行的当前任务变为空

### Requirement: 任务队列表
任务队列表 SHALL 列出：执行中的任务及其状态；排队中的任务，按快照给出的顺序，带优先级；以及远程任务的编号、所分配的车和状态。每份新快照到达后，表格 SHALL 整体按新快照刷新。

#### Scenario: 排队顺序
- **WHEN** 快照中 `queued_mission_ids` 为 `[m3, m1]`，`queued_priorities` 为 `[5, 1]`
- **THEN** 任务队列表中排队部分依次是 `m3`（优先级 5）、`m1`（优先级 1）

### Requirement: 状态栏
主窗口状态栏 SHALL 显示：数据源（回放文件名或 ROS）、任务中枢状态 `mission_state`、执行中的任务号、队列长度，以及最近一份快照距今的秒数。最近一份快照超过 3 秒没有更新时，SHALL 标为"快照过期"。

#### Scenario: 快照过期
- **WHEN** 已经 4 秒没有收到新快照
- **THEN** 状态栏显示"快照过期"，以及距今的秒数

### Requirement: 地图只画路网
地图 SHALL 以路网的 `stations` 与 `routes` 为准：每个站点画成一个点并标出编号，每条路段画成一条线段；单向路段带箭头，`enabled=false` 的路段画成虚线。坐标采用地图坐标系，y 轴朝上。不提供底图时，SHALL 只画路网，并自动缩放到能看到全部站点。提供底图（地图 yaml 加图像）时，SHALL 按 yaml 里的分辨率和原点把底图与路网对齐。

#### Scenario: 没有底图
- **WHEN** 只收到车间路网，没有提供底图
- **THEN** 地图显示全部站点和路段，视图正好装下全部站点

#### Scenario: 底图对齐
- **WHEN** 提供的底图 yaml 中 `resolution=0.05`、`origin=[-10, -5, 0]`，站点 `s` 的坐标是 `(0, 0)`
- **THEN** 站点 `s` 画在底图像素列 200 处，以及距底图底边 100 像素的那一行上

### Requirement: 车辆位置与朝向
地图 SHALL 把每辆车画在它最新的位置上，标出朝向和车辆号；还没有收到过位置的车辆不画。

#### Scenario: 位置更新
- **WHEN** 收到 `robot_3` 的一条位置，`x=2.0`、`y=1.0`、`yaw=1.5708`
- **THEN** 下一帧中 `robot_3` 画在 `(2.0, 1.0)` 处，车头朝 y 轴正方向

### Requirement: 路段按占用着色
地图 SHALL 按交通状态的占用表给路段和站点着色。`route_edge:A__B` 对应 A、B 之间的路段，两个方向都算；`route_node:S` 对应站点 S。占用者是车辆时，SHALL 用该车的颜色，同一车辆在整个会话中颜色不变；占用者不是车辆时（如 `traffic_block:` 开头），SHALL 用统一的封锁样式。无法解析的资源号 SHALL 忽略并计数。资源不再出现在占用表中时，SHALL 恢复默认样式。

#### Scenario: 车辆占用路段
- **WHEN** 占用表中 `route_edge:jn_s__machining` 的占用者是 `robot_2`
- **THEN** `jn_s` 与 `machining` 之间的路段画成 `robot_2` 的颜色

#### Scenario: 释放后恢复
- **WHEN** 下一条交通状态中这条资源不在占用表里
- **THEN** 该路段恢复默认样式

### Requirement: 等待指示
交通状态中每一条等待（资源，等待车）SHALL 在地图上画一条虚线，从等待车的当前位置指向所等的资源：路段指向其中点，站点指向站点本身。`deadlock_robot_ids` 中的车辆 SHALL 突出显示。等待消失或车辆离开等待环时，对应的虚线或突出显示 SHALL 随之去掉。

#### Scenario: 三车成环
- **WHEN** 交通状态中 `robot_1`、`robot_2`、`robot_3` 各在等一个被另一辆车占着的资源，且 `deadlock_robot_ids` 为这三辆车
- **THEN** 地图上有 3 条虚线，分别从这三辆车指向各自在等的资源，三辆车都突出显示

### Requirement: 交通面板
交通面板 SHALL 显示：占用表（资源、占用者）；等待队列（资源、等待车、优先级、队内位置，同一资源按队内位置排序）；死锁信息，包括当前环上的车辆、累计检测次数、检出次数、解开次数、按改道 / 退避 / 暂停分类的次数、最近一次解除的动作和让路车及耗时、单次检测耗时（最近一次与历史最大）。最近一次解除耗时为 -1 时，SHALL 显示为"尚未解开"。

#### Scenario: 尚未解开
- **WHEN** 交通状态中 `last_resolution_ms` 为 -1
- **THEN** 交通面板中最近一次解除的耗时显示为"尚未解开"

### Requirement: 冒烟度量
`robot_console` SHALL 支持冒烟模式：给定时长 N 秒和输出文件，从同时收到第一份快照和第一份路网时开始计时，N 秒后写出两列（`metric`、`value`）的 TSV 并退出，退出码为 0。TSV SHALL 包含：快照、交通状态、车队事件的条数与频率；每车位置更新的条数，以及其中最低的频率；服务超时次数；位置延迟的 p50、p95、最大值（毫秒）；帧数、帧率、单帧绘制耗时 p95。120 秒内一直没收到快照或路网时，SHALL 不写文件，以退出码 2 退出。

#### Scenario: 位置延迟的定义
- **WHEN** 某一帧绘制时，`robot_2` 画出的位置相比上一帧有更新
- **THEN** 记一个延迟样本，值为该帧绘制时刻的系统时间减去这条位置的 `stamp`；本帧位置没有更新的车辆不记样本

#### Scenario: 栈没起来
- **WHEN** 以冒烟模式启动，但 120 秒内没有任何快照
- **THEN** 进程以退出码 2 退出，不写输出文件
