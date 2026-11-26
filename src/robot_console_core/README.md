# robot_console_core

Qt 调度台的核心部分：数据源接口、回放、模型层、地图、交通面板、主窗口和冒烟度量。只依赖 Qt 6（Core、Gui、Widgets、Test），不依赖 ROS。连接真实系统的 RosBridge 在 `src/robot_console` 包里。

行为规格和回放文件格式见 [operator-console 规格](../../openspec/specs/operator-console/spec.md)。

## 在 Mac 上编译和测试（不需要 ROS）

```bash
brew install qtbase
cmake -S src/robot_console_core -B ~/build/robot_console_core -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build ~/build/robot_console_core -j
ctest --test-dir ~/build/robot_console_core --output-on-failure
```

构建目录放在仓库外面，免得被 rsync 同步到虚拟机。

Mac 上的 Qt 比虚拟机和 CI（6.4.2）新。推送前在虚拟机上跑一次：

```bash
colcon build --packages-select robot_console_core robot_console
colcon test --packages-select robot_console_core robot_console && colcon test-result --verbose
```

## 对着回放开发

```bash
~/build/robot_console_core/robot_console_replay src/robot_console_core/test/data/mini.jsonl --loop
```

参数：`--speed 4` 四倍速，`--loop` 循环，`--map <地图 yaml>` 加底图，`--quit-after <秒>` 定时退出（自动化检查用）。

- `test/data/mini.jsonl`：手写的小数据，只用于单测，不来自真实运行。
- 真实录像由 `scripts/console_capture.py` 在夜跑第 5、6 阶段录制，留在虚拟机的 `~/nightly/<日期>/` 下。`scripts/console_replay_trim.py` 可以从中截一段。

地图操作：滚轮缩放，左键拖动平移，中键恢复自动适配。

## 实时操作

在 Ubuntu 使用已启动的 ROS 栈，加载工作区环境后打开调度台。底图配置可省略。

```bash
source install/setup.bash
ros2 run robot_console robot_console
```

- 在「操作 → 工作流」加载目录，选择场景和工作流，设置优先级后下单。订单号会填入查询框。也可输入已有订单号并查询，再暂停、恢复或确认取消。勾选「包含运行中子任务」会把这一选择随控制请求发送。
- 在「操作 → 搬运」选择取货、送货站点和优先级。受理后，操作记录显示任务号、分配车辆和后端返回的任务编号。远程搬运按任务号跟踪。本地搬运按任务中枢编号跟踪。
- 在地图右键点击路段，可直接进入对应路段的封路和解封操作。车队表双击指定车辆可进入调度资格设置。停用只更新车辆的调度资格，车辆运行状态及遥测继续由快照展示。
- 在「操作 → 运维」选择资源并预览。核对占用者、将释放的资源和将解除暂停的车辆，随后确认清理。发生冲突时重新查看刷新后的影响，再决定是否确认。调度台分别显示服务回执和实际观测，超时会显示「结果未知」。
- 使用 `robot_console_replay` 打开录像时，仍可查看地图和状态。操作页仅在实时窗口中提供。

## Ubuntu 桌面入口

构建工作区后，在工作区根目录执行以下命令。生成的实时入口连接当前 ROS 环境。回放入口可在桌面文件管理器中对 JSONL 文件使用「打开方式」。

```bash
source install/setup.bash
ros2 run robot_console install_desktop_entry "$PWD/install/setup.bash"
```

轻量仿真需要显式启用 `fleet_sim_stop_enabled:=true`，启用后初始停止。调度台「操作 → 仿真急停」中确认解除后，仿真代理才继续运动。
