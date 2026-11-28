## ADDED Requirements
### Requirement: 路权实例纪元与租约代次分配
路权服务每次实例启动时 SHALL 生成非零 64 位 `service_epoch`，在所有申请应答及 `/traffic/grants` 的 `granted` 与 `replan` 通知中返回该非零纪元；新锁定资源 SHALL 分配单调递增代次填入授予应答与 `granted` 通知的 `resource_revisions`（`replan` 为空），未释放期间同车幂等重申 SHALL 返回原纪元与原代次。
#### Scenario: 排队重规划与幂等重申均携带当前非零实例纪元
- **WHEN** 路权服务实例以非零纪元 `e2` 运行，对已获授车辆处理幂等重复申请，并对封路受阻的排队车辆发布 `event="replan"` 的 `/traffic/grants` 通知
- **THEN** 幂等申请应答返回原 `(e2, resource_revisions)`，`replan` 通知携带 `service_epoch=e2` 与空 `resource_revisions`

### Requirement: 路权释放二元租约守卫
释放请求带非零 `expected_service_epoch` 与等长正整数 `expected_revisions` 时，系统 SHALL 仅释放纪元等于当前 `service_epoch`、仍归该车持有且代次匹配的锁，纪元或代次不匹配项 SHALL 幂等忽略；基于新版接口重编后零纪元加空数组 SHALL 按车辆号释放；参数仅传其一、含零代次或长度与非空 `resource_ids` 不符 SHALL 拒绝。
#### Scenario: 同一进程内车辆重新获授后收到旧租约的迟到释放
- **WHEN** 在同一路权服务实例 `e1` 内，`robot_1` 先以租约 `(e1, r1)` 持有资源并发出带 `expected_service_epoch=e1, expected_revisions=[r1]` 的释放请求，在该旧释放包滞留网络期间 `robot_1` 已完成释放并以新代次 `(e1, r2)`（`r2 > r1`）重新获授同一资源，随后滞留的 `(e1, r1)` 释放请求才抵达服务端
- **THEN** 服务端忽略该过期的 `(e1, r1)` 释放，路权表中该资源仍归 `robot_1` 以 `(e1, r2)` 持有，不误授予其他等待车辆
#### Scenario: 路权服务重启后收到重启前旧实例的同代次迟到释放
- **WHEN** `robot_1` 在重启前旧路权服务实例 `e1` 中以首轮代次 `(e1, 1)` 持有资源并发出带 `expected_service_epoch=e1, expected_revisions=[1]` 的释放请求，随后路权服务重启生成新实例纪元 `e2`（`e2 != e1`）且代次计数器归零重新分配，使 `robot_1` 在新实例中再次以 `(e2, 1)` 获授同一资源，此时重启前滞留的 `(e1, 1)` 释放请求抵达新实例
- **THEN** 新实例因 `e1 != e2` 忽略该跨重启迟到释放，路权表中该资源仍归 `robot_1` 以 `(e2, 1)` 持有，不误释新锁
#### Scenario: 新版接口重编后使用默认零值与空数组的无守卫释放调用
- **WHEN** 基于新版接口重新构建的 CLI 或调用方以默认 `expected_service_epoch=0` 且空 `expected_revisions` 请求释放 `robot_1` 持有的资源
- **THEN** 服务端按原有车辆号归属规则释放对应资源
