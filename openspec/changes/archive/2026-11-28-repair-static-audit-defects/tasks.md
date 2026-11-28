# Tasks

- [x] 1.1 修复核心 C++/ROS 属实缺陷（`P0-01`、`P0-03`、`P1-05`、`P1-06`、`P1-07` 零接口改动部分、`P2-01` 注释）并补充纯函数单测。
- [x] 1.2 修复夜跑、Dagu、REST/Outbox 与 VDA5050/PLC 桥接脚本属实缺陷（`P0-04`、`P0-05`、`P0-06`、`P1-08`、`P1-09`、`P1-11`、`P2-10`）并补齐离线 Python 单元测试。
- [x] 1.3 修复工程治理、守卫工具、CI/Docker 与文档属实缺陷（`P1-12`、`P2-03`、`P2-06`、`P2-07`、`P2-08`、`P2-09`）并补齐离线单元测试。
- [x] 1.4 完成本地离线全量校验（`bash -n`、`py_compile`、全部 `lint_*.py`、`unittest`、`openspec validate --strict`、`robot_guard.py verify-change`）与独立核验。
- [x] 1.5 在 VM 恢复空闲且经用户授权后完成 `colcon build` 与 `colcon test` 实机编译单测，并纳入基于最新主线的冻结快照通过正式共同夜跑验收（核对 `source.manifest` 与零问题 `postcheck.json`）。
