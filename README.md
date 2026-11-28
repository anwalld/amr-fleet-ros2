# robot

基于 **ROS 2 Jazzy** 的移动机器人全栈工程仓库，覆盖底盘与传感器接口、导航与路径跟踪、任务调度与多车交管、设施协同、操作员控制台、Gazebo 仿真及自动化验收治理。

## 架构与工作树拓扑

- **目标运行环境**：Ubuntu 24.04 + ROS 2 Jazzy（开发与 CI 容器见 `docker/Dockerfile` 与 `.github/workflows/ci.yml`）。
- **主检出目录**：`/Users/mima0000/CLionProjects/robot`（宿主机 LaunchAgent、`scripts/dagu_chat.py` 与 `tools/robot_guard.py` 的权威运行入口）。
- **独立 Worktree 目录**：`/Users/mima0000/CLionProjects/robot-worktrees/<branch-dir>`，各并行变更共享 Git common dir；查看当前工作树拓扑可执行 `git worktree list --porcelain`。

## 21 个 ROS 2 包分层索引

| 分层 | 包名（位于 `src/`） | 职责概览 |
| --- | --- | --- |
| 接口定义（8 包） | `robot_interfaces_core`、`robot_interfaces_navigation`、`robot_interfaces_mission`、`robot_interfaces_site`、`robot_interfaces_facility`、`robot_interfaces_fleet`、`robot_interfaces_business`、`robot_interfaces` | 领域拆分的消息/服务/动作接口包及汇总兼容包 |
| 硬件与感知（3 包） | `robot_description`、`robot_hardware`、`robot_sensors` | 机器人 URDF/xacro 描述、底盘驱动与里程计、IMU 等传感器解析 |
| 控制与运动（3 包） | `robot_teleop`、`robot_path_tracking`、`robot_navigation` | 速度多路复用与安全门控、路径跟踪控制器、地图与导航管理 |
| 任务与协同（1 包） | `robot_tasks` | 任务执行器、动作序列导航、交通预约/防死锁、设施控制与站点配置管理 |
| 仿真与启动（2 包） | `robot_simulation`、`robot_bringup` | Gazebo 仿真集成、多车代理/仿停节点、整栈 launch 与参数编排 |
| 控制台（2 包） | `robot_console_core`、`robot_console` | 操作员控制台核心逻辑库与桌面 UI |
| 实验与工具（2 包） | `robot_experiments`、`robot_utils` | 路径跟踪实验统计、系统健康监控与故障监督 |

## 构建、测试与静态门禁速查

```bash
# 1. ROS 2 构建与单测（Ubuntu / ros:jazzy 环境）
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
colcon test --parallel-workers 2
colcon test-result --all --verbose

# 2. 零依赖静态门禁（宿主机与 CI 通用）
python3 scripts/lint_assertion_alternation.py
python3 scripts/lint_case_map.py
python3 scripts/lint_interface_coverage.py
python3 scripts/lint_script_timeouts.py
python3 scripts/lint_service_response.py
bash scripts/check_robot_interfaces_split.sh
python3 -m py_compile scripts/*.py tools/*.py
bash -n scripts/*.sh
```

- **夜跑互斥锁门禁**：验收脚本与夜跑编排通过文件锁互斥保护共享仿真/ROS 环境，禁止在锁持有期间并发抢占或绕过锁门禁。

## 规范、守卫与工作流导航

- **规格基线**：[`openspec/specs/`](openspec/specs/) 存放各能力域的 OpenSpec 权威规格；在手变更位于对应 worktree 的 `openspec/changes/<change>/`。
- **统一守卫工具**：[`tools/robot_guard.py`](tools/robot_guard.py) 提供 OpenSpec 变更覆盖核验（`verify-change`）、缺陷链计数（`check-defects`）、上下文压缩计数（`check-compaction`）与双宿主钩子入口（`hook`）。
- **Dagu 编排与跨工具派单**：参见 [`dagu/README.md`](dagu/README.md)（VM 夜跑工作流 `dagu/robot-nightly.yaml` 与 Mac 派单门禁 `dagu/codex-apply.yaml`）。
- **项目协作规则与交接索引**：参见 [`AGENTS.md`](AGENTS.md) 与 [`docs/handoffs/README.md`](docs/handoffs/README.md)。
