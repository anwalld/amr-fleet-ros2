# Design

## Context

动机见 proposal.md。基线代码为 `main` @ `cd4bde4`（M2 收尾后）。现有资料里行为描述散在三处：二开计划与交接文档（设计意图）、`docs/baseline/M0.md`–`M2.md`（实测记录）、代码注释与 GoogleTest。本变更把其中"对外可观察、后续里程碑会依赖"的部分收拢成规格。

## Goals / Non-Goals

**Goals:**
- 每条需求都对应到现有实现（代码或测试），规格描述的是现在的行为，不是期望的行为。
- 能力边界按"以后谁会改它"划分，让 M3、M4 的变更各自落在一两个能力上。

**Non-Goals:**
- 不修任何发现的问题；不为了好看而改写现有行为。
- 不覆盖业务订单目录、设施联动、地图、Gazebo 场景等 M3–M5 不打算改的部分。

## Decisions

**1. 四个能力，扁平目录。** `route-reservation`、`station-transport`、`fleet-robot-agent`、`acceptance-validation`。项目没有已有的目录层级，扁平最简单；按领域分层（如 `traffic/…`）等能力超过十个再说。
- 备选：把站点运输并进路权。否决：M3 改死锁只动路权，M4 调度台只读路权和站点运输状态，分开后各自的增量更小。
- 备选：不为工具写规格。否决：验收运行器、自动确认器和压测判定是里程碑结论的来源，夜间批处理（M3 起）也要依赖它们的输出格式。

**2. 写可观察行为，不写类名。** 规格里出现的是服务名、话题名、状态字符串、消息格式、资源命名和参数名，这些是脚本和其他节点真正依赖的契约；`TrafficService`、`Plan*` 函数等内部结构只在本文件提及。

**3. 旧接口按"保持兼容"写成一条需求。** 五个旧路权服务的名字和类型、以及当前死锁检测只报冲突候选的语义，单独成一条需求。M3 会对这条做 MODIFIED，届时增量清楚地显示"从冲突候选改为等待图找环"。

**4. 对照方法。** 每条需求写之前读对应实现：路权读 `traffic_reservation.cpp` 与 `traffic_service.cpp`，站点运输读 `station_transport_workflow.cpp` 与 `mission_runner_node.cpp` 的滚动派发段，代理读 `fleet_robot_agent_node.cpp` 与 `fleet_sim.launch.py`，工具读对应脚本。tasks.md 的核对任务再逐条复查一遍，并以现有 GoogleTest 与 M2 记录为证据。

## Risks / Trade-offs

对照代码时发现的出入与隐患（只记录，不在本变更修）：

- [路段锁命名在三处各写一份：`traffic_reservation.cpp`、车队代理、压测 Python 脚本] → 任一处改了排序规则，代理释放或压测判定会静默对不上。M3 动资源模型时考虑让代理和脚本从服务应答里取资源号，而不是自己拼。
- [占用者有两种身份：whole_route 下任务中枢用任务号，rolling 下用车辆号] → 清除预约、死锁检测按"占用者"聚合时两种身份混在一起。规格如实写出；M3 的等待图以车辆为节点，需要决定 whole_route 是否也改用车辆号。
- [解封失败的消息写的是 `route resource is reserved by mission: <占用者>`，占用者其实可能是车辆号] → 只是措辞，规格不约束这条消息文本。
- [补授予时整组拿不齐的高优先级等待者会被跳过，低优先级的先走] → 这是 M2 的有意选择（避免一辆车堵住整条队），但存在高优先级车长期拿不到的可能；规格写明了"跳过"，M3 引入解除策略时一并考虑。
- [站点运输的上货/卸货确认规则、导航失败与取消规则没有单元测试，`station_transport_workflow_test.cpp` 只覆盖入队门控] → 规格依据的是 `station_transport_workflow.cpp` 的实现和验收脚本的整栈运行；M3 以后改到站点运输时先补这几组 GoogleTest。
- [车队代理（`fleet_robot_agent_node`）没有单元测试，行为证据只有 M2 的压测记录] → 规格依据实现代码；它的状态机和寻路写在节点里，没有抽成纯函数。
- [规格基于单次读码，可能有遗漏] → tasks.md 里逐条复查，并跑 `openspec validate --strict`。

## Migration Plan

无代码改动。归档后 `openspec/specs/` 生成四份主规格；回滚即删除这四个目录。
