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
