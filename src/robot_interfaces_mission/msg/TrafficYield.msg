# /traffic/yield：死锁解除时发给让路车的指令（M3）。
# action：reroute = 绕开 avoid_resource_ids 改走 station_path 去原目的地；
#         backoff = 沿 station_path 退到避让点（最后一站），停留 hold_s 秒后再去原目的地；
#         pause   = 反复成环兜底：停在原地不再申请，直到持有的路权被运维清除。
builtin_interfaces/Time stamp
string robot_id
string action
string[] cycle_robot_ids
string[] avoid_resource_ids
string[] station_path
float64 hold_s
