# 任务控制接续与合并夜跑

## 接手时先查

- 从[分类索引](../README.md)进入本交接与[遗留事项](unassigned-follow-ups.md)。主检出旧路径为兼容入口。分支 OpenSpec、缺陷 TSV 及原始验收产物从该索引和[共同夜跑登记](../nightly/shared-nightly.md)定位。
- 在主检出用 `git worktree list` 查候选工作区，在该工作区核对 `git status --short --branch`、两个变更的 `tasks.md` 与 `verification.md`。新版重启对账已完成本地实现和隔离核验。正式验收项 4.1 是否可勾选，以对应冻结快照的整轮终态和专项原始断言为准。
- 从[共同夜跑登记](../nightly/shared-nightly.md)查当前作业及冻结快照。简报缺少终态时核对 Dagu 原生结果与启动日志。旧轮 `merged-control-current-full` 的 8/8 已记录在[终态交接](../nightly/current-full-closeout.md)。本对话负责本地核验与回填，合并作业由协调对话负责。

## 11 月 26 日 21:01 接续核对

额度已重置，5 小时剩 99%，周剩 43%。两个独立 worktree 的遗留事项正文一致，已收敛到分类索引下的共用记录，并在各自原路径保留入口。

新版作业 `merged-restart-reconciliation-full` 的 VM 启动日志记录 `Result: Aborted`，与共同登记中的 18:08 停止记录一致。简报仍读出最后一次巡检的 2/8、验收 23/110 和旧 ETA。两项 4.1 继续开放，旧轮正式通过证据已分别回填，当前新版保留重启 4/4 和十项隔离轨迹证据。候选作业及恢复范围见共同夜跑登记。

## 11 月 26 日额度接续记录

18:09（北京时间）核对，5 小时剩 2%、周剩 44%，触发 5 小时暂停阈值，重置时间为 21:00:02。复用本对话 `robot-4`，安排 21:01 接续。已观测上下文压缩 1 次。

合并候选 `codex/merged-nightly-candidate` @ `58b9000` 保留既有未提交源码、OpenSpec 与验收产物。D16 和远程车队各 4/5，均待 4.1。Dagu 新版作业 `merged-restart-reconciliation-full`（冻结 `58b9000-merged-e8c4e462`）当时通过 2/8，编译 21/21、单测 695/695、验收 23/110，预计 22:57。VM 产物目录为 `/home/ubuntu/nightly/2026-11-26-merged-restart-reconciliation-full/`。

接续先重查额度、三个任务工作区和 Dagu 状态。核对两个实施分支的遗留事项文档是否含独有内容。取得新版终态后对照冻结清单，复核 8/8、110/110、六组连续压测、四项重启、十项过程轨迹与 `shared-rework` 后验，再回填两个 OpenSpec 核验记录及 4.1。保留既有本地改动。提交、推送和跨对话投递沿用原授权边界。

## 历史交接

2026-11-25。用户要求本地推进可独立完成的工作，整栈验收放进合并夜跑。主检出、Ops/Alarm 独立检出及既有改动均保留。当前对话已观测上下文压缩 0 次。

| 变更 | 实施 worktree 与状态 |
|---|---|
| D16 | `/Users/mima0000/CLionProjects/robot-worktrees/coordinate-mission-pause`，`codex/coordinate-mission-pause` @ `58b9000`，有未提交源码、接口和设计，任务 2/5。 |
| 远程任务控制 | `/Users/mima0000/CLionProjects/robot-worktrees/remote-fleet-task-control`，`codex/remote-fleet-task-control` @ `58b9000`，有未提交源码、接口和设计，任务 2/5。 |

本轮按探索流程评估本地快照、在线重建和未知时停驶，设计各附「重启边界与合并验收」。任务中枢、导航序列和远程车队代理增加可配置的进程重启保护。复用已存在的 checkpoint 路径或创建失败时，禁止派发、新导航目标或代理运动。代理查询返回 `RECOVERY_REQUIRED`。该机制只冻结，完整任务、路权和回执的恢复对账仍待实施。两个工作区 `git diff --check` 已通过。Linux 运行验证尚未通过。

VM 独立构建目录 `/tmp/robot-d16-local.swAwZQ` 和 `/tmp/robot-remote-local.1Mj6o8`。D16 本轮 `restart-build.log` 记录导航节点编译链接成功，任务中枢 c++ 编译进程被 SIGKILL，原因待判。远程代理顺序构建因此尚未启动。启动时间约北京时间 00:34，原估 1 至 2 分钟，判据为三节点重新编译链接及隔离重启检查通过。作业已终止，无运行中的本任务后台编译、测试或待复查计时。VM 预置的 `/tmp/robot-restart-check.py` 已写入，未因旧二进制误判通过。该脚本启动两轮进程，检查复用路径时任务中枢、导航目标和代理控制的阻断。适用证据目录仍保留上轮成功的测试。

合并夜跑使用同时包含两任务变更及 Ops/Alarm 的可追溯快照。保留共同回归基线、Ops/Alarm 阶段 7 独立验收和 Dagu 终态核验，加入远程行驶与暂停中取消、真实 Alarm 急停下拒绝恢复、rolling 与 whole_route 的占用及移车、D16 双导航、延迟回执、等路权危险和重启案例。下方「下一次合并夜跑范围」是本轮待验变更与专项清单。等可测快照就绪后，按清单登记待验变更，使用实际作业 ID 和 ETA。

额度检查见工具记录，5 小时剩余 9%，北京时间 04:11:30 重置，周剩余 10%。继续前重查额度、worktree、VM 身份与可用内存。先区分 SIGKILL 原因，使用独立 VM 构建目录重试剩余三节点，清除可能残缺的测试对象时只动该隔离目录。构建后分别在隔离 ROS 域执行预置脚本（`RESTART_CASE=d16` 和 `RESTART_CASE=fleet`，分别从对应目录启动并 source 其 overlay）。然后补跨进程实际状态对账、D16 丢应答/等路权专项、合并验证接线，并按 /opsx:verify 更新证据。提交、推送或开 PR 的授权维持项目原边界。

同一对话已建立一次额度恢复接续，自动化 ID `robot-3`，安排北京时间 04:12。该接续用于额度重置后的实施工作。

## 本对话接手核对

用户要求同时处理这份合并夜跑及其数据。00:36 检查 VM 的正式夜跑目录和 Mac Dagu 登记，未发现包含 D16 和远程任务控制的正式作业。同时在跑的 `m4-ops-alarm-nightly-r2` 登记了 Ops、Alarm 与 rolling 退避修复，已通过编译和单测，不能用于确认 D16 或远程任务控制专项。恢复时先检查另一个对话或 VM 是否已推进同一构建或合并作业，复用已有日志并避免对同一工作区重复写入。检查合并快照须同时包含 D16、远程任务控制和 Ops/Alarm，尤其要核对两分支共有的任务中枢改动与远程代理和 Alarm 改动的组合。没有匹配的正式终态时，先完成上文构建、专项和证据接线。取得终态后逐项核对各变更的实际断言与原始数据、Dagu 后验和既有 Ops/Alarm 对照，再分变更回填 verification 与 tasks。

## 用户随后明确指的是已结束的旧跑数据

00:39（北京时间）本对话复查，5 小时额度剩 4%，周额度剩 9%，在 11 月 25 日 04:11:30 重置 5 小时额度。用户明确排除准备中的 D16 / 远程任务控制合并跑，以及正在运行的 Ops/Alarm r2。之前「本对话接手核对」的 D16 验收计划继续由原实施接续处理。

恢复后先核对旧数据的两个可追溯入口：11 月 24 日已结束的 Ops + Alarm `m4-ops-alarm-nightly`（登记 Ops 和 Alarm，正式失败，原报告、13 条巡检、压测与后续复核都在 Ops worktree 的证据目录与交接）；以及 11 月 23 日 M4-core + M1–M3 验收重整共同跑 `m4-core-rework6-r2`（主检出收尾交接显示已补六车 whole_route、回填主线、归档并通过 CI）。主检出 `main` @ `58b9000` 有原有未提交内容，Ops worktree 分支 `claude/m4-console-ops-6389da` @ `d87cfa8` 有原有未提交内容。重查 VM 历史报告、各变更的任务与原始证据，列明实际验收目的、已经回填的位置和仍缺的处理，补实锤缺项。保留原始失败、归档结论与已有工作，不重复写入，也不把正在跑的作业当作本次目标。

## 合并夜跑执行归属

用户最新明确由另一个窗口按本文计划发起合并夜跑。本对话只接续 D16 和远程任务控制的本地实施、隔离构建、单测、隔离 ROS 验证及证据记录。上文涉及启动、绑定或接管合并夜跑的动作归另一个窗口执行。本对话提供可追溯的 worktree、验证脚本和待验收清单。以前记录的旧跑数据回查不改变这里的任务归属。

本次确认 5 小时额度剩 2%，周剩 9%。保留已建立的 04:12 额度恢复接续，恢复后按本文核对两处 worktree，再查明任务中枢编译 SIGKILL 的原因。两项 OpenSpec 任务仍各 2/5。

## 已结束的合并夜跑数据回查结果

2026-11-25 04:12 后复查。`m4-ops-alarm-nightly` 是当时最近一轮已结束的合并正式作业，验收 Ops 实际窗口写操作、Alarm 恢复与仿真停车，以及共同全栈回归。Dagu 终态 6/8，阶段 3 的故障监控和旧接口审计失败，阶段 5 四车 rolling 停摆。补齐首轮阶段 3 的 110 项原始结果、两条失败日志及 trace、阶段 7 的 Ops 与 Alarm 原始 QtTest 和运行日志。在该轮 VM 快照上独立执行证据函数，将输出保存为 `independent-review.json`，与原始 Dagu 终态分开。两项 OpenSpec verification 已回填该轮实测，Ops 10/10，外部封路地图刷新 235 毫秒，冒烟业务发布为零，Alarm 3/3。原报告和巡检校验和均一致，阶段 3、5 失败照旧，正式通过基线及任务待办仍按共同验收结果处理。见 Ops worktree 的 `docs/baseline/nightly/2026-11-24-m4-ops-alarm-evidence/`。

另核对更早的 M4-core + M1–M3 验收重整共同跑 `m4-core-rework6-r2`。主线报告和 VM 原报告 SHA-256 一致。重算原始六车 whole_route 及同快照专项，分别为 1644 条、最大缺口 157.11 秒和 1800 条、最大间隔 1.03 秒。补测 20 单、零违规，退出码 0。M4-core 及验收重整均已归档，补测和阶段 7 已在主线基线，已有回填无需重复。D16 与远程任务控制的合并跑仍按上节的窗口分工。

## Ops/Alarm r2 终态给合并窗口的输入

2026-11-25 05:29 北京时间，r2 通过 7/8 个阶段。两项上轮失败已通过，四车 rolling 恢复且死锁场景 9/9。仅阶段 3 回充和重定位失败。Ops worktree `/Users/mima0000/CLionProjects/robot-worktrees/m4-console-ops-6389da` 已更新 `scripts/check_return_to_dock.sh` 和 `scripts/check_relocalization_resume.sh`。同 r2 快照、正式渲染环境三轮各两项全部通过。合并候选需纳入这两份脚本，并沿用原阶段 3 用例与判据。原 r2 仍为 108/110，Ops/Alarm 完整验收待通过。详见该 worktree `docs/handoffs/m4-console-ops.md` 和 `docs/baseline/nightly/2026-11-25-m4-ops-alarm-r2-evidence/`。


## 下一次合并夜跑范围

- 移出 11 月 23 日已完成验收并归档的 M4-core 与 M1–M3 验收重整。采用主线的验收重整基线和已归档的六车 whole_route 同快照补测，无需再为旧采样缺口额外安排补测或单列旧变更。 [M4-core 收尾](../archive/m4-console-core-closeout.md)
- 移出 11 月 24 日已经在 11 月 25 日 r2 正式夜跑通过的三个修复专项。故障监控与旧接口审计在 r2 的阶段 3 各自 PASS，rolling 退避修复完成 OpenSpec 3/3，阶段 5 六组与阶段 6 九场均通过。这三项不再单独排复跑或作为本次待验变更登记。合并快照包含修复，标准回归沿用既有用例表和压测矩阵。
- 保留 Ops、Alarm 的共同验收。本次登记 m4-console-ops、m4-console-alarm、coordinate-mission-pause 和 remote-fleet-task-control，实际启动时核对合并快照包含四项。回充与重定位的新脚本已连续三轮各自通过，两项仍须随共同验收核验。回执超时的传输层根因待整栈重新观察。 [r2 报告](../../../../robot-worktrees/m4-console-ops-6389da/docs/baseline/nightly/2026-11-25-m4-ops-alarm-r2-evidence/report.md)

## 11 月 25 日 11:58 启动前核对

用户要求启动下一轮合并夜跑。VM lm-worker-1 可达，夜跑锁空闲。D16 和远程任务控制仍各 2/5，尚无包含四项变更及两套新专项的可运行快照。对三个工作区的交叉改动做了只读三方合并检查，任务中枢、车队代理和仿真包 CMake 各有一处冲突。现有 VM 日志属于各分支的隔离构建或专项，没有合并候选的验收输出。本次未启动。合并窗口需整合上述改动，并在同一候选接入 D16 与远程任务控制专项，之后按本页范围启动 Dagu，登记四项并按实际阶段预算记录 ETA。

## 11 月 25 日 12:12 本地重试

重查额度时 5 小时剩 90%，周剩 5%。VM lm-worker-1 可用内存约 6.5 GiB，内核日志无本次编译被杀记录，SIGKILL 根因尚未证实。12:12:34 只读核对夜跑无进程后，在两个原隔离目录串行重编译及执行重启脚本，当前对话工具会话 48042。D16 和车队分别使用 ROS 域 210、211，仅限本机发现。每组上限 175 秒，预计北京时间 12:18:24 前结束。检查各目录的 restart-attempt2.log 和 restart-check.log。完成判据为当前源码重新链接成功，两个隔离场景均输出首次启动与复用标记通过。本次活动仅限本地构建和测试。

## 11 月 25 日合并冲突处理

用户授权解决三处冲突。在独立候选 /tmp/robot-merged-nightly.3OxFia 以 main 58b9000 为底，叠加 Ops/Alarm d87cfa8、D16 和远程任务控制两个 58b9000 工作区的当前改动。仿真包保留两组测试。车队代理共用一个急停订阅、同一消息新鲜度判定，保留重启保护、远程停稳确认及告警急停优先。任务中枢 Tick 同时维护暂停对账和远程控制超时。三个来源工作区的改动保留。

VM 候选位于 /home/ubuntu/robot-dagu-workspaces/merged-nightly-candidate.r0IgIQ/robot_ws。源清单 SHA-256 为 aa41391882718d9517d40bd981564352e07403c604c962b2aa1ecd6bf3741fce，两端已核对。北京时间约 12:12 开始编译四个相关包，预计按两个接口包生成约 45 秒、两个 C++ 包低并发约 4 至 7 分钟在 12:17–12:20 完成。构建日志及退出码见同级 control/merge-build.log 和 control/merge-build.exit。完成判据是 Ubuntu 四包编译通过，并在同一候选验证急停、任务停稳和暂停组件。此构建不是共同夜跑。D16 和远程任务控制尚余的验收与专项接线继续按本文范围追踪。

12:15 阶段检查发现周额度剩 4%，5 小时剩 88%，停止新增工作。D16 的任务中枢编译在多编译进程并行时超过本次 140 秒上限，隔离脚本随后误跑了旧二进制，因而不能用该断言结果判断新实现。导航节点链接已通过。远程代理的同一批复查从 12:15:05 开始。续接先核对正在收尾的批次和两个日志，再重试任务中枢。重试命令必须用 `编译 && 运行测试` 或显式 return 保护，确保编译失败后不会执行旧二进制。安排同一对话的周额度接续 robot-4，按上次观察到的触发时区将下次设在 UTC 14:02 对应北京时间 11 月 29 日 22:02。旧接续 robot-3 已清理。合并夜跑仍由另一窗口负责。

12:16 观察到批次终态。远程代理重编译后通过首次启动与复用标记两轮断言，证据在 `/tmp/robot-remote-local.1Mj6o8/restart-attempt2.log` 和 `restart-check.log`。D16 仍待任务中枢重新链接后的有效重启试验。两项任务仍各 2/5，重启标记验证只覆盖启动门控。批次已退出。

12:17 核对两个接口包和仿真包已通过，任务中枢机器人任务包仍在编译。仿真 CTest 两个目标均通过，日志 control/simulation-merge-tests.log。实际代理在两个开关同时开启时成功启动，ROS 图确认单个急停订阅及远程任务控制服务，日志 control/agent-combined-graph.log。ROS 检查结束已清理进程和释放锁。周额度剩 4%，11 月 29 日 22:01:28（北京时间）重置，已安排原对话 22:02 接续。先确认原构建终态与最新候选，再运行 pause_state_test、fleet_runtime_state_test、traffic_deadlock_test。共同夜跑未启动。

交接前构建已终态，四包全部通过，merge-build.exit 为 0。后续接续先复用该终态，只需运行已编译的三项任务中枢定向测试并核对后续合并验收就绪情况。

## 用户临时授权继续本地验证

本轮临时放宽项目额度阈值。用途为当前两项变更的本地构建与隔离验证。D16 在空闲 VM 上单独重编译，原慢跑的上限由 140 秒调整为 240 秒，之后隔离测试 45 秒。作业名 `d16-restart-idle`，VM 已有隔离目录内的 `restart-idle-build.log` 和 `restart-idle-check.log` 记录判据。仅在当前源码成功编译链接后进入测试。

## 临时额度授权后的候选核验

用户为本次续接临时放行项目额度暂停阈值。合并候选源清单重新逐项通过。任务中枢的 pause_state_test、fleet_runtime_state_test、traffic_deadlock_test 均通过，日志在 VM 候选同级 control/task-merge-tests.log。实际合并产物复用既有重启脚本，在各自隔离的 ROS 域验证 D16 与远程任务控制首次启动及复用检查点，两项均通过。日志、退出码与实际启动配置留在 control/restart-check.Cf9glr/。该证据补齐合并候选的启动保护检查。完整重连对账、远程任务控制的 Ops 界面和两项共同夜跑专项接线仍待完成。共同夜跑未启动。

本轮临时授权后，D16 在空闲 VM 上单独编译并通过重启保护。迟到应答、等路权期间定位丢失和独立直线里程计均通过隔离测试。见 D16 verification.md 及 VM `restart-idle-check.log`、`pause-reply-retry.log`、`route-hazard.log`、`motion-odometry.log`。本轮所有隔离作业均已结束。D16 3/5，远程任务控制 2/5。点检两条分支时读取其各自 verification。下一步是重启后的权威数据恢复对账及在合并快照上的专项证据接线，由合并窗口负责整栈运行。

## 任务控制重启专项接入 Dagu

按用户要求在同一独立合并候选实现接线。候选脚本 scripts/run_task_control_restart.sh 复用已验证的首次启动和重复检查点断言，分别启动 D16、远程车队代理，输出独立日志与结果表。阶段 7 已接入并纳入退出码，Dagu shared-rework 后验要求两条 PASS 和实际断言记录。Ops、Alarm 的独立证据仍照常验收。

Ubuntu 合并产物两个专项均 PASS，原始证据在 VM 候选同级 control/restart-stage7.IboK9Y/task-control-restart/。缺结果、缺断言的反例均被后验拒绝。Dagu 阶段 7、8 真入口 dry-run 通过，日志在 control/restart-dagu-preflight.log。更新后的两端源清单 SHA-256 为 b7cb65a0b211d467b2f17df7289f07e0d36e6b4689f690158c7e048dbd208f18。重启子场景已经接线，完整重启联机对账、远程任务控制的目标选择和回显，以及两项剩余仿真场景仍独立待验。

## 四项合并候选正式夜跑

独立 worktree /Users/mima0000/CLionProjects/robot-worktrees/merged-nightly-candidate，分支 codex/merged-nightly-candidate，HEAD 58b9000，含未提交合并实现、Qt/ROS 测试和阶段 7 确认。VM 候选 /home/ubuntu/robot-dagu-workspaces/merged-control-nightly.zpQgIf/robot_ws，源清单 SHA-256 27227984783870a68cb45259252aa47bf6c578c40e8cc41f90bbf7c970cf15e5。四项 OpenSpec 严格校验通过，固定参考 110/110。两项重启门控、五项仿真场景（D16 迟到回执和危险中路权授予，车队 rolling、whole_route、暂停后取消）已在合并产物验证通过。Ops 新增远程任务目标选择、查询与回执展示，断线后以原任务号、执行代次和请求修订查询。Qt 与 ROS 测试已通过，保留回放只读。进程重启保护验证通过，进程重启后的完整运行态重建仍待完成。

根据上轮用时约 5 小时 8 分钟，加入阶段 7 专项启动与取证，估算本次约 5 小时 10 分钟（北京时间）。Dagu 预检通过后启动 merged-control-nightly，启动日志在候选同级 control/launch.log。完成判据为八阶段通过、Dagu shared-rework 后验及四项变更在本候选的专项证据通过。正式结果以该作业实际输出为准。

2026-11-25 12:37:44（北京时间）正式启动，12:37:46 进入编译。正式输出 /home/ubuntu/nightly/2026-11-25-merged-control-nightly/，Dagu 初始 ETA 17:48:38，首次周期复查 13:07:44。Dagu 已绑定本对话和 Ops、Alarm、D16、远程任务控制四项。旧专项按上文已通过范围移出，本次标准回归继续包含既有用例。终态后核对结果，清理本机 com.codex.robot.merged-control-nightly-awake 标签。

17:38（北京时间）宿主重启后只读复查。Mac 于 17:35:23 启动，Fusion 当前运行 VM 数为 0，lm-worker-1 的 VM 日志末条约 17:33:48。Dagu 登记尚未收到终态，本机经旧 VM 地址无法连入。待 VM 恢复后优先核对该次 nightly.log、checks.jsonl、报告及压测原始采样，确认重启时的实际阶段和是否已有终态，再依据证据决定补跑范围。

### 宿主重启后的同快照补跑

VM 启动被旧锁阻塞。确认 Fusion 无运行实例、VM 文件无打开句柄后备份旧锁，并在 APFS 副本上使用 Fusion 磁盘工具修复。原始磁盘保存在 /Users/mima0000/Virtual Machines.localized/.lm-worker-1-recovery.Lr46yD/lm-worker-1-original.vmdk。修复副本由 Fusion 启动，Ubuntu 17:44:28 启动、SSH 恢复，原快照源清单核对通过。

原作业阶段 1/2/4 通过，阶段 3 七条失败、阶段 5 四车 rolling 停摆 1103 秒均发生在重启前。第 6 阶段 7/9 场景有通过记录，重启发生在 outside_wait 第 2 遍，原阶段 6 未写判定，阶段 7/8 未开始。为保持阶段 6 原始矩阵完整，使用原构建和源清单补跑 6–8，预计约 20–23 分钟（原阶段 6 历史 10–12 分钟，阶段 7 历史约 10 分钟）。新作业 merged-control-recovery，判据为三阶段都通过、9/9 场景和全部阶段 7 原始证据齐全。原作业日志和失败保留。

17:45:27（北京时间）补跑正式启动，17:45:29 进入第 6 阶段，预计 18:09:10 完成，已绑定本对话和四项变更。原始中断证据和重启前失败已另存于合并 worktree 的 docs/baseline/nightly/2026-11-25-merged-control-interrupted-evidence/。已确认原阶段 3 为 103/110，其中接口覆盖缺四条新服务登记。原阶段 5 四车 rolling 有完整采样但停摆，保留为原失败。补跑仅补齐 6–8，终态后对照两次原始证据。结束后清理 com.codex.robot.merged-control-recovery-awake。

### 重启前失败修复与专项复验

修复继续使用独立合并候选 `/Users/mima0000/CLionProjects/robot-worktrees/merged-nightly-candidate` 的 `codex/merged-nightly-candidate` 分支，基准 HEAD `58b9000`，保留四项未提交实现及原始快照。补登记四条接口、将 D16 危险路权验收迁移到统一下单接口、以持续订阅和可观察的 DDS 就绪条件核验故障停车、隔离 REST 验收 ROS 域并记录请求与失败日志。四车 rolling 在退避时按实际停车位归属优先选择可通行的公共避让点或本车车位，保留原回退。OpenSpec 严格校验通过，补齐共享车位边界后重验规划单测。

补跑阶段 6 于 17:57 完成，死锁 9/9，接续阶段 7。专项首次并行编译在执行补跑清场期间收到 SIGKILL，原日志保存在 VM `/home/ubuntu/robot-dagu-workspaces/merged-hardening.QQXzzx/build.log`。改在 VM 夜跑锁内单线程编译并串行验证。重试作业进程为 14285，输出位于 `/home/ubuntu/robot-dagu-workspaces/merged-hardening-retry.z496Of/`，源码和复用构建位于同级 `merged-nightly-candidate.r0IgIQ/robot_ws/`。

以补跑预计 18:09 完成、编译与七项原失败用例及五项任务控制专项约 15–20 分钟、滚动压测 30 分钟，暂估北京时间 19:00 前后复查终态，18:30 复查中途结果。完成判据是编译、七条原失败验收和五条任务控制子场景全通过，四车 rolling 30 分钟采样连续，零占用违规、无停摆和节点提前退出。逐项核对 `build.exit`、`stage3/results.tsv`、`task-control/results.tsv`、`rolling.exit` 和原始日志；若有复现，按失败调用和栈日志继续定位。最终合并验收仍以修复候选共同夜跑及四变更专项证据为准。

18:07 补跑终态 PASS，阶段 6–8 全通过。另按原始文件复核死锁 9/9、九份回放记录类型齐全、冒烟和回放、Ops 10/10、Alarm 3/3、重启门控 2/2、任务控制仿真 5/5。原始报告、巡检和独立后验在合并 worktree 的 `docs/baseline/nightly/2026-11-25-merged-control-recovery-evidence/`，补跑防睡眠断言已释放。此证据来自原快照，只补充原来被中断的阶段。

18:08 专项复验锁内编译已通过（两个包，39.4 秒），接口覆盖 162/162，停车位归属修复后的规划单测 32/32。七项原失败用例由专项进程继续串行执行。其本机防睡眠断言为 `com.codex.robot.merged-hardening-awake`，作业终态后清理。四车专项增加只读回放采集以记录停摆时的等待者和路权占用。

18:14 专项验收 7/7、任务控制仿真 5/5，含改用统一下单的 D16 危险场景。车队派单、路权和操作快照使用原用例通过，原故障根因尚待证据。四车 rolling 已于北京时间 18:14:37 开始采样，预计 18:45 可取终态。已从运行中的任务中枢读取实际参数 `park_1,park_2,park_3,park_4`。专项源码清单 1011 个文件逐一比对 Mac 和 VM，一致，SHA-256 `f5c5d5bcab58e6422d6d8a01f89444d32b42de0925816adbaa14890bb8b69561`。证据快照在合并 worktree 的 `docs/baseline/nightly/2026-11-25-merged-control-hardening-evidence/`，终态时补齐四车日志。

18:21 直接查询原作业 Dagu 状态为 Failed，说明进程异常终止，旧进程已失效。Dagu 巡检 JSONL 因宿主重启没有终态行，简报仍可能显示运行中。保留原始证据，Dagu 直接查询另存 `dagu-status.txt`。

四车专项采样至 743 秒时，最长全队静止 6 秒，死锁检测和解除均为 8，已越过原停摆起点。后续正式共同夜跑的 VM 冷构建快照已按相同源码清单生成于 `/home/ubuntu/robot-dagu-workspaces/merged-control-repair-nightly.WG9CwY/robot_ws` 并通过 Dagu schema 预检。预计全量 5 小时 10 分钟，等待四车专项终态及原始证据核验后再启动。

### 修复专项终态及正式共同夜跑

18:44（北京时间）专项通过。按原始轨迹、任务表、违规表和进程日志重算，四车 rolling 连续 1800 条、最大间隔 1.01 秒、54 单（15/14/13/12）、零违规、零提前退出、最长全队静止 6 秒，20 次死锁全部解除。证据见 `docs/baseline/nightly/2026-11-25-merged-control-hardening-evidence/rolling-review.json` 及同目录原始文件，完整回放保留于 VM 专项目录。专项防睡眠断言已清理，缺陷账本检查无升级。

正式作业 `merged-control-repair-nightly` 于北京时间 18:45:35 启动，18:45:37 进入编译，预计 23:56:29 完成，首次周期巡检 19:15:35。冷构建快照源清单 SHA-256 已核对，标识 `58b9000-merged-f5c5d5bc`。输出目录 `/home/ubuntu/nightly/2026-11-25-merged-control-repair-nightly/`，启动日志在快照同级 `control/launch.log`。Dagu 已绑定本对话和 Ops、Alarm、D16、远程任务控制、rolling 停车位优先五项变更。沿用前述 11 月 23、24 日已验收专项剔除范围及完整标准回归。

完成判据为八阶段、`shared-rework` 后验和五项对应的原始证据通过。终态后复制巡检、报告及后验，核对 110 件验收、四车 rolling 原始连续采样、Ops/Alarm 及两项任务控制的专项断言。若通过，将已核验条目回填至各 OpenSpec，完整重启在线对账仍按其原判据审核。若失败，定位重复失败的实际调用和栈。收尾清理 `com.codex.robot.merged-control-repair-nightly-awake`，保留 Dagu 巡检直到项目归档条件满足。提交、推送、开 PR 未获本轮授权。

### 用户合盖断网前暂停正式作业

2026-11-25 20:59:40（北京时间）按用户要求对 `merged-control-repair-nightly` 执行 Dagu 正常停止。Dagu 原生状态为 `Aborted`，已确认仿真及夜跑进程退出、VM 夜跑锁空闲，本机防睡眠断言已释放。原目录及源码和编译产物均保留。巡检 JSONL 未写取消终态，简报可能沿用旧运行状态，以 Dagu 原生状态为准。日志、验收结果和阶段 5 指标表已备份到合并 worktree 的 `docs/baseline/nightly/2026-11-25-merged-control-repair-paused-evidence/`。

暂停前阶段 1、2、4 已通过（21 包、688 项单测、演示四项），阶段 3 已结束，109/110，原失败为 `amr_sim:gazebo_entities`。阶段 5 的双车 whole_route 已完成，1800 条采样、19 单、零违规；双车 rolling 中途停止，保留部分采样。阶段 6–8 未开始。

用户恢复后，先核对 VM 已醒、锁空闲、冻结源码清单和现有构建。用原快照 `/home/ubuntu/robot-dagu-workspaces/merged-control-repair-nightly.WG9CwY/robot_ws` 和新 Dagu 作业 ID 接续阶段 5–8（`ONLY_STAGES=5 6 7 8`），保留原输出。现有入口按阶段接续，阶段 5 重新从头跑以产出同一作业完整压测矩阵，预计约 3 小时 25 分钟，按 Dagu 实际 ETA 复查。保留原阶段 3 的失败并另核对日志；后半程通过也不改写该结果。五项绑定沿用原登记，未启动接续作业。

### 11 月 26 日同快照接续

用户恢复后通过 VMware Fusion 启动原虚拟机，SSH 恢复。原工作区源码清单逐项核对，SHA-256 为 `f5c5d5bcab58e6422d6d8a01f89444d32b42de0925816adbaa14890bb8b69561`，安装产物存在，锁可用。候选分支 `codex/merged-nightly-candidate` 基准 HEAD `58b9000`，保留既有修改。用原构建于北京时间 2026-11-26 01:44:58 启动 `merged-control-repair-resume`，01:45:00 进入第 5 阶段。首次周期巡检 02:14:58，预计 05:09:36 完成（六组各 30 分钟加死锁、调度台和报告预算）。输出 `/home/ubuntu/nightly/2026-11-26-merged-control-repair-resume/`，启动日志在原快照同级 `control/resume-launch.log`。五项变更已绑定原会话，防睡眠断言 `com.codex.robot.merged-control-repair-resume-awake` 待终态释放。

本次完成判据是阶段 5–8 全部通过、原始六组压测采样后验通过、调度台与任务控制各项证据复核通过。分段作业使用 Dagu 的默认后验，终态再逐项复核阶段 6–7 原始证据。保留原阶段 3 的 109/110；`amr_sim:gazebo_entities` 的日志显示 `payload_box_waiting` 到期限仍停在初始位姿，原因待查。继续作业的结果单独记录。

### 11 月 26 日续跑终态核对

北京时间 05:08:02，`merged-control-repair-resume` 退出码 0，阶段 5–8 全部通过，后验零问题。原快照源清单逐项通过，VM 夜跑锁在终态已释放，Mac 对应该轮防睡眠断言已清理。报告、巡检、后验及独立复核见合并 worktree 的 `docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/`。六组实测各 1800 条，无违规、进程提前退出或 rolling 全队停摆。四车 rolling 58 单、最长全队静止 4 秒。独立核验九组死锁回放与六车 rolling 回放、Ops 10/10、Alarm 3/3、重启门控 2/2、任务控制仿真 5/5，已回填五项 OpenSpec。退避修复 3/3。原第 3 阶段 109/110 保留，整轮正式验收及完整在线重启对账仍开放。当前运行环境未提供 Codex 用量查询工具，额度未知。

05:10 左右以同一快照和正式渲染参数，持 VM 夜跑锁仅复验 `amr_sim:gazebo_entities`。输出 `/home/ubuntu/nightly/2026-11-26-gazebo-entities-recheck.LyDHSJ/`，查看 `acceptance/results.tsv`、`runner.log` 和 `exit`。按上轮 81 秒失败或历史完整演示约 4 分钟估计，预计 05:14 左右得到结果；终态以原断言通过和退出码为准。

05:14:21 前，Gazebo 原判据同快照复验终态 PASS，耗时 250 秒、退出码 0，末尾原断言已核对，VM 锁已释放。本地原始证据在续跑证据目录的 `gazebo-entities-recheck/`，包含用例日志、trace 及运行中保留的自起栈日志。原作业因货箱未离开初始位姿而失败，本次继续经过路权、避障解除与限速恢复断言，原失败未复现，账本保留 pending。未更改验收判据或正式 109/110 原结果。接续得到阶段 5–8 成功证据及缺项单例通过证据，整轮全绿基线和完整在线对账仍开放。

### 11 月 26 日任务清单证据回填

在合并 worktree `/Users/mima0000/CLionProjects/robot-worktrees/merged-nightly-candidate` 回填两项变更。原跑和续跑均为 `58b9000-merged-f5c5d5bc`。D16 合并清单补勾 1.1，原第 3 阶段两种导航用例通过，续跑等待路权时定位丢失和暂停回执迟到通过。远程任务控制补勾 3.1，已从同快照的阶段 2 留存 Qt 回放及断线对账与 ROS 标识传递的原始 XML。合并工作区两项各 3/5，独立工作区不据此勾选尚未合入的界面工作。

两项均仍需完整在线重启对账，以及按任务目标、时间、位姿和路权贯通的专项原始轨迹。正式第 3 阶段保留 109/110，单例 Gazebo 同快照原判据 250 秒通过，原失败根因未确认。只核对并回填证据，本对话未启动夜跑。

### 11 月 26 日 Gazebo 验收和重启对账接续

合并 worktree 仍在 `codex/merged-nightly-candidate`、`58b9000`。原失败的 Director 在北京时间 19:16:48.791 到达 WAIT_LOAD，19:16:49.992 确认装货；验收 trace 于 19:16:57.962 结束。桥的持久日志为空，无法确定剩余约 8 秒内到底是状态接收还是 Gazebo 更新的问题。已将搬运前置阶段和货箱位姿等待分开，保留原货箱位姿表达式、60 秒检查及登记表 600 秒总超时。失败时留自起栈日志片段和有界的 payload 查询。

`gazebo-acceptance-timing.ghwICV` 是独立 VM 快照，只更新验收脚本，沿用既有二进制并复核源码清单。标识 `58b9000-merged-4e28d2e4`。定向验证日志位于快照同级 `control/targeted/`，基于上次完整 250 秒的实测预计本次约 4–6 分钟。判据为原全部断言通过。

OpenSpec Explore 确认了重启保护的两个入口缺口。任务中枢重复标记只能挡住自身派发，外部路权申请仍能从刚创建的空表获得授予。原任务不存在于新进程索引时，远程控制服务会返回不存在。已在两项设计中记录取舍。对账前停止新的路权授予和远程派单，空表不再作为已知状态，远程命令返回待对账并携带原身份。完整在线对账仍须恢复或确认旧目标、可验证位姿、实际占用和回执，不能从空表恢复自动起步。

独立 VM 工作区 `restart-authority-gate.uRfwk5` 的回归作业已排队，和 Gazebo 专项串行占用 VM 锁。证据放同级 `control/`。先用原二进制运行增强重启测试，要求复现原版本仍授权外部路权；再按 Release 重编 robot_tasks，以新二进制复验 D16 和远程代理。根据上次任务包用时和本次冷覆盖构建，预计锁释放后约 2–4 分钟，判据为两项通过和原回归断言。正式阶段 3 将在定向验收通过后启动并单独留证。

Gazebo 调整后的单例在原二进制上 PASS（243 秒、退出码 0）。留证在合并 worktree 的 `docs/baseline/nightly/2026-11-26-merged-control-repair-resume-evidence/gazebo-timing-followup/`。原失败的桥接/Gazebo 层根因仍不确定，保留账本 pending。

重启门控已取得修复前后对照。原二进制重复标记下仍授予 `route_node:receiving`，修复二进制拒绝授权和放行，旧远程命令回显身份并报告待对账。robot_tasks Release 编译通过，D16/车队重启专项 2/2，任务包定向 CTest 5/5，两项设计和验收场景回填。两项任务仍各 3/5；在线重建并获得放行证据尚未完成。修复边界及源清单见 `restart-authority-gate.uRfwk5` 的 `control/` 与合并 worktree 的 `docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/`。

北京时间 11:18:57 启动 `merged-control-stage3-recheck`，11:18:59 进入第 3 阶段。使用 `58b9000-merged-4965ab34`，预计北京时间 12:50:57 完成，首次巡检 11:48:57。VM 正式输出 `/home/ubuntu/nightly/2026-11-26-merged-control-stage3-recheck/`，启动日志为快照同级 `control/stage3-launch.log`。已绑定本对话和五项变更。完成判据是登记表 110/110、报告和零问题后验；这是阶段 3/8 专项，不能回填为一次八阶段齐全的整轮通过。终态时取回 `checks.jsonl`、报告、结果及本次 Gazebo 原始现场，并清理 `com.codex.robot.merged-control-stage3-recheck-awake`。如失败，先按实际日志处理，不以单例覆盖历史结论。

### 11 月 26 日本地实现接续

已在远程任务控制与合并候选 worktree 接入取消后驻留的重启恢复。记录原执行、取消修订、停车位姿和占用，并持有检查点目录的独占锁。重启须核对同一交通发布端的实时快照、路权模式和全部占用，以及调度端同车的任务取消终态。成功后保持驻留，显式移车前作废停车记录。远程独立工作区与合并候选各自留有源码改动，原正式快照不变。D16 本次新增运动切单原始轨迹，完整的活动导航目标重建仍开放。

合并候选补接下一次专项的过程 JSONL，包含控制请求、回执、动作代次、位姿、停稳窗口和路权。增加 D16 运动切单、驻留重启和丢失路权反例。VM `task-control-trace.0n9NEC/evidence-r5` 是隔离复测，ROS 域 218。因另一窗口阶段 3 会按安装路径清理进程，本轮使用同组件的隔离二进制目录，当前会话 40472，预算约 1–3 分钟，硬上限 350 秒。判据为八例通过且每例 JSONL 通过后验。此批只运行任务控制专项，正式第 3 阶段仍由另一窗口负责。

隔离批次 40472 已结束，使用另存的任务中枢、导航、Alarm 二进制及本轮重新编译的远程车队代理，避免另一窗口正式阶段 3 清场。八例全部 PASS。JSONL 逐例后验通过，结果及原始记录留在合并工作区 `docs/baseline/nightly/2026-11-26-task-control-isolated/`。另保留在同一阶段 3 启动期间被全局清场中断的 VM r4 结果，未计入通过。合并候选源码相对旧正式快照已改变，下一次正式运行需重新生成源清单。D16 活动导航目标/队列重建与远程活动任务重启恢复还开放。合并阶段 3 作业归原窗口。

### 11 月 26 日正式阶段 3 终态与最新候选整轮

北京时间 12:39:20，`merged-control-stage3-recheck` 通过，退出码 0，阶段 3 身份及运行栈与 110 条登记逐项匹配，全 PASS。Gazebo 270 秒，日志同时含装货确认和动态实体通过。阶段 8 报告完成，后验零问题，巡检包含 11:48、12:18 两次定期检查及终态。原快照源码清单逐项验证，报告、巡检、原始结果和完整现场在合并 worktree 的 `docs/baseline/nightly/2026-11-26-stage3-recheck-evidence/`。已释放旧防睡眠断言。旧 109/110 保留，历史货箱未更新的底层原因未确认。

已用合并候选最新源码生成新清单，VM 按文件核对，一致。新修订 `58b9000-merged-59a37fbf` 含驻留车辆重启恢复、失去路权反例及八例过程轨迹。源目录 `/home/ubuntu/robot-dagu-workspaces/merged-control-current-full.YlN4Os/robot_ws`。新 Dagu 作业 `merged-control-current-full` 于 12:43:39 启动，阶段 1 进行中，预计北京时间 17:54:33 结束，首次周期巡检 13:13:39。输出 `/home/ubuntu/nightly/2026-11-26-merged-control-current-full/`，快照同级启动日志 `control/launch.log`。五变更均绑定本对话。终态核对八阶段原始证据、110 条验收、六组 30 分钟连续采样和 `shared-rework` 后验，独立核对八例轨迹。清理 `com.codex.robot.merged-control-current-full-awake`，保留巡检数据至归档条件满足。D16 活动目标和队列重建、远程活动任务对账仍待实现和核验。

### 11 月 26 日活动任务接续

在远程任务控制 worktree 补已确认停稳的活动任务断点。普通行驶段和站点装卸记录路径进度或停留剩余时间；核对同一交通端、任务 PAUSED、实时占用和原执行。恢复为暂停，显式 RESUME 前作废旧断点。运动中断电、未停稳和不能证明位置的情况继续待对账。在 D16 worktree 与合并候选修正恢复未确认就显示 RUNNING。旧二进制在延迟恢复应答的测试中复现，修复二进制保持 RESUMING，待当前修订确认及反馈后才读为 RUNNING。

独立滚动与整路两组暂停中重启实测通过。合并候选的任务控制新版隔离批次 65314 在北京时间 16:10:50 启动，ROS 域 222，目标 10/10 及轨迹后验，通常 1–3 分钟，上限 430 秒。VM 证据 `/home/ubuntu/robot-dagu-workspaces/control-paused-merged.Wra6lu/evidence`。当前正在跑的正式整轮使用此前已冻结快照，仍由另一窗口负责。隔离批次使用独立二进制前缀，避免正式阶段的路径清理相互干扰。

隔离批次 65314 已结束，十个场景原始 JSONL 后验通过，检查点单测 3/3。合并工作区留存 `docs/baseline/nightly/2026-11-26-paused-restart-and-resume-feedback/`。D16 延迟恢复应答旧版首先 RUNNING，修复版 RESUMING 后才 RUNNING，缺陷账本记录 `d16-premature-running` 已修。新版远程代理在两种路权模式均从已确认停稳的活动任务继续。正式夜跑冻结快照仍较早，本次修改不会覆盖其证据。剩余 D16 活动目标与队列重建、远程装卸断点定向实测及调度中枢重启后的索引重建。

### 11 月 26 日中枢持久化和重启对账

本轮改动落在 `/Users/mima0000/CLionProjects/robot-worktrees/merged-nightly-candidate` 的 `codex/merged-nightly-candidate`，基线 HEAD `58b9000`，仍未提交。配置独立检查点路径时，任务中枢保存任务分配、确认回执、完整排队任务、暂停来源和目标 UUID。重启后读取原队列及分配，原任务回执可从持久记录或原代理的精确查询恢复。D16 对存活的原目标按 UUID 请求并核对驻停，未能确认时保持禁止派发；路权未核对时只开放查询，代理无法领取任务。启动参数已接入普通 mission_runner 和 fleet_sim 入口。

隔离 VM 目录 `/home/ubuntu/robot-dagu-workspaces/mission-restart-state.kTmTNJ`，新中枢编译链接通过。ROS 域 226 的四项重启专项均通过，检查点单测 2/2，作业已退出。证据在合并工作区 `docs/baseline/nightly/2026-11-26-mission-restart-reconciliation/`。两项设计、规格及任务已按实际断言回填，均 4/5，只余包含新版的共同夜跑验收。已有正式作业使用更早冻结快照，本对话未启动或接管作业。合并窗口应重新生成源清单并继承已有历史证据。

### 11 月 26 日最新候选整轮终态

北京时间 17:36:56，`merged-control-current-full` 冻结快照 `58b9000-merged-59a37fbf` 已通过，八阶段 8/8，`shared-rework` 六类后验独立复算一致且无缺项。编译 21 包、单测 691、验收 110、压测六组、死锁九遍、Ops 10、Alarm 3、任务控制过程轨迹八项及重启门控两项通过。Ops 14/14、Alarm 8/8、退避 3/3，已回填。冻结后更新的 D16 与远程车队实现分别 4/5，留待新版共同跑。专项归属、按对话通知、加入和移出规则统一到 [共同夜跑登记](../nightly/shared-nightly.md)。原始报告、后验、巡检及证据索引见 [终态交接](../nightly/current-full-closeout.md)。

### 11 月 26 日新版对账整轮启动

上一轮 `merged-control-current-full` 的终态已复核，回填和移出登记沿用既有记录。新跑 `merged-restart-reconciliation-full` 于北京时间 17:46:36 启动，冻结修订 `58b9000-merged-e8c4e462`，快照 `/home/ubuntu/robot-dagu-workspaces/merged-restart-reconciliation-full.FZhiAZ/robot_ws`，文件清单在 VM 核对一致。历史八阶段预算约 5 小时 11 分钟，初始 ETA 22:57:30，首次周期检查 18:16:36。输出 `/home/ubuntu/nightly/2026-11-26-merged-restart-reconciliation-full/`，启动日志在快照同级 `control/launch.log`。

两变更已绑定协调对话，负责对话及专项通过条件见 [共同夜跑登记](../nightly/shared-nightly.md)。以 8/8、验收 110/110、六组连续压测、四项重启和十项过程轨迹原始断言通过、`shared-rework` 零问题为完成条件。终态取回原始结果、回填两项 4.1、按登记处理通知，并清理防睡眠断言 `com.codex.robot.merged-restart-reconciliation-full-awake`。

18:08 用户叫停 `merged-restart-reconciliation-full`。Dagu 原生状态 Aborted，进程退出、夜跑锁释放、防睡眠断言移除。编译 21/21、单测 695/695，阶段 3 中止。本轮原证据保留，不接续启动。先按已通过的 `merged-control-current-full` 收尾 Ops、Alarm 和退避修复，归档与合入依项目授权；重复终态不派发新跑次。
