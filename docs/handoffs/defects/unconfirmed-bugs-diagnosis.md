# 未确认缺陷逐项复核

后续的九项修复、定向证据和完整验收进度见[独立修复分支交接](../../../../robot-worktrees/verified-defect-fixes/docs/handoffs/changes/repair-verified-defects.md)。

2026-11-26，北京时间。编号沿用[原始盘点](unconfirmed-bugs.md)。每项在取得证据时登记，保留历史跑次的原判定。

核对了 5 个 worktree 的分支、HEAD、工作区改动与活动任务。主线、D16、远程车队、合并候选同为 `58b9000`，实现有未提交差异；Ops 工作区基于 `d87cfa8`。下文写“候选”时以修复版冻结清单 `f5c5d5bc…` 为准，已逐项比对合并工作区的 1,011 个文件，全部一致，VM 修复作业的清单摘要也一致。候选根目录另有旧清单 `27227984…`，对应中断前首轮。取证使用历史进程日志、当前源码、已有正式结果和离线反例。ROS 仿真由原验收对话运行。[修复版清单](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-hardening-evidence/source.manifest)

当前仍有明确故障路径的是 U10（关栈反馈异常）和 U12（终止后仍报运行中）。U13 给出了旧吞吐基准的原始轨迹重算。历史报错中，U03 发现了旧断言的竞态，U04、U05、U07 找到了对应服务的回执失败日志。U14–U21 分别列出仍开放的实现、配置及验收边界。

## U06、U07 · DDS 回执发送超时与机会充电

**U06　已定位到中间件的具体返回分支。** VM 安装的 `ros-jazzy-rmw-fastrtps-shared-cpp` 为 8.4.4。日志里的 `rmw_response.cpp:153, client will not receive response` 对应服务端已取得请求，准备发送响应时，在 writer 的 `max_blocking_time` 内未能确认匹配到该客户端的响应 reader。该分支返回 `RMW_RET_TIMEOUT`，位于实际 `write` 之前。M2 取消排队、Ops r2 回充、合并首轮车队状态更新均有这一签名。它们共享直接失败机制。每次为什么没能及时匹配，尚缺端点发现与生命周期轨迹。回充命令已执行，故客户端超时不能作为操作未生效的依据。持久客户端专项 3/3，修复候选正式阶段 3 中回充和车队派单也都通过。[Fast DDS ROS 适配层 8.4.4 源码](https://github.com/ros2/rmw_fastrtps/blob/8.4.4/rmw_fastrtps_shared_cpp/src/rmw_response.cpp#L128-L165) · [车队派单原栈][fleetlog] · [正式阶段 3][formal]

**U07　可并入 U06 的直接机制。** VM 原跑 `~/nightly/2026-11-24-m4-ops-nightly/stage3_acceptance/acceptance/` 下，`safety__opportunity_charging.trace` 显示充电策略检查已通过，查询 charger 时客户端等了 30 秒，退出码 124。同目录 `_stack__safety__opportunity_charging.log:87` 明确记有同名 `/v2/list_facility_resources` 的上述发送超时。缺少请求 GUID，不能把同服务的并发调用逐个配对。同快照复查 34 秒通过，修复候选正式用例 28 秒通过。应跟踪回执可靠性，现有材料不支持把机会充电策略列为故障。[原用例](../../../scripts/check_opportunity_charging.sh) · [正式结果][formal]

## U01　调整优先级有两次不同的失败

M1 的脚本轮询瞬时状态，两单目标仅相距 0.1 米，等待第一次 CLI 查询返回时，先执行的短任务可能已经结束。提交 `76e7800` 改为比较任务事件，消除了这种漏读。D20 第三轮用的已经是事件版。VM `~/nightly/2026-11-22-rework3/stage3_acceptance/acceptance/mission__reprioritize.log` 记录两次入队、调优成功、启动返回 `queued mission profile sent`，到 60 秒外层期限仍无预期完成记录。该次版本 `099c467` 尚无后来 `43a2f18` 增加的目标受理日志，无法用缺失的日志证明请求丢失。后续正式用例 19 秒通过。M1 的夹具原因已闭环，D20 保留为历史未定因；再发生时须串起同一目标的发送、受理和结果。[M1 记录](../../baseline/M1.md) · [事件版脚本](../../../scripts/check_mission_reprioritize.sh) · [正式结果][formal]

## U02　定位夹具直接注入协方差

原来所说的“协方差未及时收敛”不适用于这项测试。脚本先向 `/amcl_pose` 注入 1.2，再注入 0.05，节点将收到的两轴最大值与 0.5 比较。原日志至少有两种失败：VM `~/acc_m0_ab1/` 和 `~/acc_m1_rerun1/` 中始终是 `UNKNOWN/0.0`，连第一次 LOST 都没观察到；`~/acc_m0_ab4/` 和 `~/acc_m1_full/` 中最后仍是 `LOST/1.2`。各目录文件均为 `safety__localization_auto_recovery.log`。两次注入各自新建短命 publisher，固定发 8 条后退出，没有等待订阅匹配，也没记录每批数据何时进入回调。现场与漏掉一批输入吻合，具体丢在哪一段仍未证实。候选正式 13 秒通过；另一个重定位用例的匹配修复没有改到这个脚本。[注入与断言](../../../scripts/check_localization_auto_recovery.sh) · [正式结果][formal]

## U03　旧对桩脚本会提前检查重试状态

M1 重跑 1、3 和 M2 原跑、重跑 2 的 `mission__docking_retry.log` 显示请求得到 `APPROACHING`，几秒后查询到 `retry_count=0, last_failure_message='', last_recovery_action=''`，在等待充电之前退出。旧脚本将整段 CLI 输出同时匹配 `success=True` 和 `DOCKING_CONTACT_FAILED`。即使返回零条事件，请求回显里的过滤条件也带这个事件名。本次用“带过滤条件的请求＋成功的空响应”回放，两项旧判断均通过；按当前 `service_response` 只取响应，同一输入无法通过事件判断。这解释了为什么旧脚本能在恢复动作登记前读取空字段。旧临时目录已清理，不能回证每次事件查询确实返回零条。正式候选 10 秒通过，暂无对桩重试状态机失效的证据。VM 样本位于 `~/acc_m1_rerun1/`、`~/m2_batch/rerun_2/`。[当前脚本](../../../scripts/check_docking_retry.sh) · [响应过滤](../../../scripts/robot_wait.sh) · [正式结果][formal]

## U04　M1 设施工作流卡在验收查询链路的证据

VM `~/acc_m1_full/business__scenario_facility_workflow.log` 已有两步工作流受理；同目录 `_stack__business__scenario_facility_workflow.log:94` 记录服务端向 `/v2/list_mission_events` 发回执时触发 U06 的错误。M1 版本 `ca6be34` 的 FINISHED 轮询直接执行无限时的 CLI 调用，只有调用返回后才能检查 90 秒期限。该用例报外层 600 秒超时，这条链路足以解释长时间挂住；原始日志没有请求标识，无法确定卡在第几个任务。当前脚本的单次查询有界，修复候选正式 50 秒通过。另一轮 `2026-11-19-rework2` 已拿到门资源 `available=[True]`，旧断言却去找标量 `available=True`，应单独归入夹具格式问题。[当前工作流检查](../../../scripts/check_scenario_facility_workflow.sh) · [正式结果][formal]

## U05　门已预留，桥接节点仍可能一直使用关门状态

M2 原跑 `~/m2_batch/acc_full/facility__showcase.log` 中，CLI 已查到门为 `reserved`、持有者正确，Gazebo 实体坐标却是 `(-4.15, 0, 0.55)`；开门判据是 `y > 0.35`。从该次 launch 找到实际进程，VM `~/.ros/log/mission_runner_node_1878935_1794844633524.log` 在启动后约 1 秒就记录向 `/v2/get_door_state` 回执失败，同秒还有升降机和充电桩查询的同类告警。桥的 `PollDoor()` 发出查询时置 `door_request_pending_`，只有回调清位；若这一回执未到，后面的轮询都会直接返回，默认 `door_available_=true` 会让 `UpdateDoorPose()` 持续写关门坐标。这一代码路径在旧版及当前候选均存在，是比软件渲染慢更贴近现场的解释。缺客户端 GUID 和当时的 pending 值，因此归因为强线索。正式候选 51 秒通过。[桥接实现](../../../src/robot_simulation/src/amr_sim_gazebo_entity_bridge_node.cpp) · [位姿断言](../../../scripts/check_amr_facility_sequence_showcase.sh) · [正式结果][formal]

## U08　路权任务已进入模拟导航，未观测到完成

原跑已撤销排队的 A，派发 B，导航服务受理目标并回退到模拟运动。验收随后一直查询 B 的 FINISHED 事件直到期限。栈在受理后约 3 秒记录过 teleop 看门狗输出零速度；通过组在启动阶段也有同类告警。该条消息无法还原受理后的连续速度。专项沿原判据 55 秒通过，修复候选正式 48 秒通过。没有证据指向路权互斥失效。仍缺同时段目标坐标、里程计、模拟控制输出及复用器输出，无法在上游反馈停更、速度被拦和实际未到位之间定因。[原始栈](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__safety__route_locks.log) · [正式结果][formal]

## U09　操作快照失败点在首个任务的模拟运动

快照服务已经成功返回，订单也受理成功。原栈记录首个门任务在时间戳 1795586343.419 受理，约 91 秒后报告 `branch=failed`，随后同一任务重新派发、再次受理。时间与一次 Nav2 回退等待加模拟运动 90 秒预算吻合，验收最终没有查到 FINISHED。专项 41 秒、修复候选正式 42 秒通过。仍缺到目标的距离和速度时序，无法认定与 U08 同因。SIGINT 在重试后才到，关栈异常单列 U10。[原始操作快照栈][snapshot] · [模拟运动参数](../../../../robot-worktrees/merged-nightly-candidate/src/robot_bringup/launch/amr_demo.launch.py) · [正式结果][formal]

## U10　关栈时活动目标发布反馈抛出未捕获异常

原栈在 SIGINT 后直接给出 `Failed to publish feedback: feedback publisher is invalid`、未捕获的 `rclcpp::exceptions::RCLError` 和退出码 -6。候选用 detached 线程执行目标，`PublishSequenceFeedback()` 直接调用 `goal_handle->publish_feedback()`。线程检查 `rclcpp::ok()` 与实际发布之间，ROS 上下文可进入关栈；执行线程缺少相应的退出协调和异常边界。与日志相符的失败路径在当前候选仍存在。一次正常结束任务的阶段 3 PASS 不覆盖“仍有活动目标时发送 SIGINT”的时序。[原始栈][snapshot] · [当前导航实现][nav]

## U11　两次编译被杀的证据强度不同

D16 只确认编译进程收到 SIGKILL，空闲时单独编译通过。合并候选编译在补跑阶段 6 清场期间退出，VM `~/robot-dagu-workspaces/merged-hardening.QQXzzx/build.log` 明列 `mission_runner_node.cpp.o] Killed`。已核对当时阶段 6 的清场脚本按进程**完整命令行**匹配 `robot_ws/install/`；候选 `build/robot_tasks/CMakeFiles/mission_runner_node.dir/flags.make` 的编译器参数恰好含多个匹配的 include 路径。运行相互独立也会命中这一条件，后续持锁串行构建通过。对应两次 VM 启动的可读内核日志未找到关联 OOM kill，亦无信号发送者审计记录。合并跑有具体的清场误杀机制；D16 发送方仍未知。两次均不足以登记为 C++ 编译缺陷。[清场代码](../../../scripts/run_deadlock_scenarios.sh) · [两次构建记录](../changes/task-control-restart-and-merged-nightly.md)

## U12　已在两个真实作业复现 Dagu 简报状态错误

本次对 `merged-control-nightly`、`merged-control-repair-nightly` 实际执行只读 `dagu_chat.py status`，两者均显示“运行中”。前者 Dagu 原生状态是 Failed，后者按用户要求停止、原生状态为 Aborted。读取端只认 JSONL 最后一行中同时带退出码的三种终态；异常中断或外部停止时，写日志进程没有留下这一行。其余状态显示运行中，继而无法进入现有终态通知和登记清理路径。宿主重启与人工停止均可触发，独立接续作业的成功也不会改变旧作业的记录。[读取端](../../../scripts/dagu_brief_remote.py) · [原生 Failed 记录](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/dagu-status.txt) · [Aborted 现场](../changes/task-control-restart-and-merged-nightly.md)

## U13　M4 采集干扰对照使用了 M3 判废的两次吞吐

本次对 VM 九组六车 rolling 的 `state.csv`、`tasks.csv` 调用当前 `stress_stalls.measure/problems` 重算。以下六组均有 1,800 行状态。11 月 18 日 day2 和 r3 的 61 单来自两份不同的轨迹文件，均通过停摆判据。

| 跑次 | 30 分钟完成单 | 单/小时 | 最长全队静止 |
| --- | ---: | ---: | ---: |
| 11-18 day2 | 61 | 122 | 2 秒 |
| 11-18 第一轮 | 33 | 66 | 834 秒 |
| 11-18 r2 | 58 | 116 | 141 秒 |
| 11-18 r3 | 61 | 122 | 1.5 秒 |
| 11-22 rework4，开采集 | 61 | 122 | 12 秒 |
| 11-23 共同复验，开采集 | 56 | 112 | 2 秒 |

原任务 9.3 的条件是“低于此前同组最低值”。M4 沿用了含停摆样本的历史区间；剔除作废样本后，表内此前有效最低值为 122，112 低 8.20%，当时实际触发了复查条件。11 月 24 日 Ops/Alarm、25 日 r2、25 日合并首轮的六车分别为 126、128、120 单/小时，当前判据下这三组也都无全队停摆。数据确认历史对照有误，无法分离采集开销与跑间波动；定量归因还需要同构建、同环境、同种子的采集开关配对。四车专项和阶段 7 回放不改变这项结论。[M3 判废依据](../../baseline/M3.md) · [M4 原表](../../baseline/M4.md) · [原任务 9.3](../../../openspec/changes/archive/2026-11-24-m4-console-core/tasks.md)

[snapshot]: ../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__ops__operator_snapshot.log
[nav]: ../../../../robot-worktrees/merged-nightly-candidate/src/robot_tasks/src/navigate_sequence_server_node.cpp

## U14　任务与导航重启只实现了拒绝继续的保护

候选两节点各自读取默认空的 `restart_checkpoint_path`。显式传非空路径时，首次创建目录；再次使用该目录或创建出错即进入恢复保护。磁盘标记能说明“以前用过”，无法提供旧目标 UUID、暂停来源、队列、位置或路权。正式专项给两个节点传独立路径，断言第二次启用时拒绝派发、新导航目标被拒；普通 launch 未发现等价的持久路径配置。因此保护有配置前提，重启后向权威状态源对账仍是实际实现缺口。[任务中枢][runner] · [导航端][nav] · [重启专项][restart]

## U15　暂停取消的旧结论只有部分仍成立

候选默认控制通道已校验目标 UUID 和修订号；同一修订迟到应答、等路权时定位丢失、旧请求与新任务隔离等已有各自的专项证据。无目标号的旧 Trigger 仍表达“对收到时的当前目标执行”，调用者最初瞄准哪个任务无法恢复。服务取消后 action 返回 ABORTED 是已约定的兼容行为。

还有两个可从当前代码指出的时序窗口。① 等 Nav2 受理时，序列取消后最多再等 2 秒，超过窗口直接退出等待；此时尚无句柄，后续才受理的目标缺少对应的取消收尾。② FollowWaypoints 暂停时，若第一次检查结果尚未就绪，代码进入 `CancelWhilePaused()`，恢复后按当前索引重新发；成功结果恰好落在首次检查之后时，这条分支没有读取它再决定是否重发。结果在首次检查前就绪的分支已处理。两者均为可指出触发条件的代码风险，已有普通暂停恢复通过记录未精确制造这两种时序。[导航实现][nav] · [独立核验](../../../../robot-worktrees/coordinate-mission-pause/openspec/changes/coordinate-mission-pause/verification.md)

## U16　停车证据取决于哪套栈、哪种测量

候选 `fleet_sim` 已有可选车队急停；启用时消费急停状态，状态超过 2 秒不更新也停止运动。普通启动参数默认关闭，且这一门控面向运动学仿真，不等于为避障、定位、人工暂停各自配齐了保护。D16 已有直线仿真对照：运行时位移超过 5 cm，暂停 3 秒位移不超 1 cm，恢复后继续。两种 Nav2 动作的兼容性验证用假服务器，只覆盖指令与结果。

模拟导航的里程计回调只更新坐标和“曾收到”标志；计算控制量时不检查最后样本的时间戳。旧坐标可被重复使用。仅发零速度指令也无法证明车体停稳。保留为真实有效的观测边界：两种导航模式下仍需与暂停、恢复时间对齐的独立位置和速度，以及测量中断场景。[候选启动参数](../../../../robot-worktrees/merged-nightly-candidate/src/robot_bringup/launch/fleet_sim.launch.py) · [急停时效判断](../../../../robot-worktrees/merged-nightly-candidate/src/robot_simulation/include/robot_simulation/fleet_sim_stop.hpp) · [里程计使用][nav]

## U17　远程车队重启丢失对账所需的运行态

启用远程控制且配置标记路径时，代理复用目录会答 `RECOVERY_REQUIRED`。实际持有资源 `held_`、任务执行代次、控制修订及回执表保存在进程内。标记中没有持久化这些内容，也没有以实际车位、调度任务、路权持有者重建它们的步骤。进程还活着时按原凭据重查，有结果可读；进程被替换后，原回执表不能再回答。两种路权模式的途中取消、驻留和显式移车已有定向通过。保留的缺口是跨进程恢复，以及运维清占用时如何与仍驻留的车辆核对；目前没有这两项已导致实际错误移动的运行证据。[车队代理][agent] · [重启断言][restart] · [独立核验](../../../../robot-worktrees/remote-fleet-task-control/openspec/changes/remote-fleet-task-control/verification.md)

## U18　车队代理有两类不同的丢应答后果

任务代理的站点、路网、任务查询、路权申请四类已有逾期原样重发。剩余释放路径 `Release()` 先从本地 `held_` 擦除，再发服务请求，未消费结果。若请求未执行，本车与服务端持有表会分歧；若服务端已执行而回复丢失，占用可能已经正确释放。任务和车队状态上报也直接发送，缺少确认。另一条跟随模式路径里，站点查询、车队查询、到站上报三个 pending 标志只在回调清零；回调始终不到，对应动作就无法再次发起。这些代码路径仍存在。现有证据能确定失败条件，不能据此推算出现频率或认定某次停摆由它造成。[代理实现][agent] · [D18 原范围](../../../openspec/changes/archive/2026-11-24-acceptance-suite-rework/design.md)

## U19　让路兜底应按三种情况判断

六车历史样本中，环内车辆的可用出口均被上游等待者占住，规划器找不到合法退路，触发暂停，压测又没有人工解冻。U13 重算证实两轮全队长时间静止。候选的停车位归属优先能在**有多个可走候选**时改变退避点；它无法为没有出口的拓扑创造空间。Gazebo 由任务中枢自驱的站点车被选为让路车时，交通层撤销等待并发布指令，原站点运输路径按原目标重申请；完整改道退避属于既定按需能力。轻量代理另有明确边界：绕过临时避让仍找不到合法路径时留在 Route 重试，非 pause 让路指令先缓存在 `pending_yield_`，到 WaitGrant 才消费；pause 指令在订阅回调立即处理。四车专项覆盖了停车位选择，未覆盖这三个边界。[M3 现场](../../baseline/M3.md) · [规划器](../../../../robot-worktrees/merged-nightly-candidate/src/robot_tasks/src/traffic_deadlock.cpp) · [代理实现][agent]

## U20　可用一个反例复现停摆判据的盲区

本次向生产函数 `stress_stalls.measure/problems` 输入 0–120 秒、每秒一行的双车样本。两车各有一单，第一辆位置始终不变，第二辆每秒移动。程序算出单车最长静止分别为 120 秒和 1 秒，全队最长静止 1 秒，rolling 的问题列表为空。实现只用全队 ≥60 秒不动或单车全程零单判失败；当前规格正是这样定义的，单车静止虽有输出，却不参与判定。此反例证明测试看不见一种停摆，不能当成实车停摆记录。

交通状态按 1 Hz 快照取样，同一秒中的多次变化可能只有累计值保留，逐次因果无法仅靠快照还原。RViz 看板已读完整字符串；另查到 `check_amr_sim_visualization.sh` 和 `check_amr_sim_default_demo.sh` 的 MarkerArray 查询仍用默认长度。具体匹配项只有落到截断位置后才会误判，未见这两例据此失败的实录。[停摆函数](../../../scripts/stress_stalls.py) · [可视化夹具](../../../scripts/check_amr_sim_visualization.sh) · [演示夹具](../../../scripts/check_amr_sim_default_demo.sh)

## U21　通知送达、对话续步和归档清理各有不同判据

本机 `merged-control-recovery` 绑定有 `sent=2026-11-25T18:07:27…`、`pending_wake=false`、`done=true`。代码在队列入队后请求恢复对话，也将“已有活动写入端”处理为结束；绑定中没有“空闲对话已启动新轮次并消费终态”的确认。因此该记录能证明通知路径进展，不能回证空闲唤醒。真实共同作业登记的变更仍有活动目录；清理入口须由归档收尾调用，并检查所有登记变更已归档、分支与目录匹配、已完成投递及同一终态。守卫未满足时保留日志符合预期。U21 当前是端到端验收条件未齐；两份缺终态的旧作业另有 U12 的真实故障。[投递与清理守卫](../../../scripts/dagu_chat.py) · [任务 5.3](../../../openspec/changes/robot-dagu-nightly/tasks.md)

[runner]: ../../../../robot-worktrees/merged-nightly-candidate/src/robot_tasks/src/mission_runner_node.cpp
[agent]: ../../../../robot-worktrees/merged-nightly-candidate/src/robot_simulation/src/fleet_robot_agent_node.cpp
[restart]: ../../../../robot-worktrees/merged-nightly-candidate/scripts/check_task_control_restart.py

## 新发现 · 修复候选唯一失败项为货箱位姿

`amr_sim:gazebo_entities` 在正式阶段 3 用了 81 秒，失败的是脚本内层 60 秒的货箱位姿断言，最后一次读到初始坐标 `(-3.35, -0.45, 0.18)`。VM 同一次 launch 的导演进程日志 `~/.ros/log/amr_sim_demo_director_node_118180_1795605338048.log` 显示，19:16:48 到达 WAIT_LOAD，19:16:49 确认装载成功。脚本从第 21 秒就开始等货箱，等待窗口包含回充、脱桩、运输至装货点等前置动作。演示桥收到已装载状态后还会先保留原位 2 秒，随后插值抬升。由此可确认，测试的大部分等待时间花在业务前置动作上；剩余窗口内，桥是否及时收到了 payload/odom，以及 Gazebo 何时应用新位姿，缺少同时间线记录。这是货箱场景的真实失败，不能用 U05 的门板解释替代。[失败与最后位姿](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage3_acceptance/acceptance/amr_sim__gazebo_entities.log) · [完整跟踪](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage3_acceptance/acceptance/amr_sim__gazebo_entities.trace)

## 原清单末尾的正式验收状态

- 修复候选的阶段 3 已完整执行，109/110。回充 8 秒、重定位 14 秒、故障监控 1 秒、REST 工作流 17 秒、旧接口审计 5 秒、接口覆盖用例 1 秒，均通过；旧 REST 超时前没有逐步现场，无法反推具体阻塞点。上述用例已获得正式阶段证据。判定全栈时仍须保留货箱失败。[正式结果][formal]
- 四车 rolling 的停车位优先修复，已在原拓扑、种子和 30 分钟专项中得到 54 单、1,800 行、零违规、全队最长静止 6 秒。修复版正式全跑在阶段 5 双车 rolling 期间依用户指令停止，原生状态 Aborted。11 月 26 日的同快照接续只跑阶段 5–8，本次核对时处于阶段 5，由原对话负责。四车正式与后验结论以该作业实际输出为准。[四车专项](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-hardening-evidence/rolling-review.json) · [作业交接](../changes/task-control-restart-and-merged-nightly.md)
- 11 月 25 日 `merged-control-recovery` 是中断前旧快照的补跑，阶段 6–8 已通过，原始复核含死锁 9/9、Ops 10/10、Alarm 3/3、重启保护 2/2、任务控制仿真 5/5。该成绩只归属这一次快照和对应子场景。[补跑独立复核](../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-recovery-evidence/independent-review.json)

[fleetlog]: ../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/stage3_acceptance/acceptance/_stack__business__fleet_dispatch.log
[formal]: ../../../../robot-worktrees/merged-nightly-candidate/docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/stage3_acceptance/acceptance/results.tsv
