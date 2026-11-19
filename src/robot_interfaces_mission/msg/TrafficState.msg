# /traffic/state（1 Hz）：路权占用表与各资源的等待队列。
builtin_interfaces/Time stamp
string mode
# 占用表：resource_ids[i] 由 owner_ids[i] 持有（车辆号、旧模式的任务号或 traffic_block:原因）。
string[] resource_ids
string[] owner_ids
# 等待队列，每行一个（资源, 等待者），同一资源内按队列顺序排列。
string[] waiting_resource_ids
string[] waiting_robot_ids
int32[] waiting_priorities
int32[] waiting_positions
uint64 grant_count
uint64 queue_count
# M3 死锁统计（等待图检测与解除）。
# 累计检测次数、累计检出的新环数。
uint64 deadlock_check_count
uint64 deadlock_count
# 当前等待环上的车（多个环合在一起，已排序）。
string[] deadlock_robot_ids
# 最近一次解除：动作（reroute / backoff / pause）、让路车、耗时（毫秒；-1 = 尚未解开）。
string last_resolution_action
string last_resolution_robot_id
float64 last_resolution_ms
# 累计已解开的环数（每解开一个加一，压测按它的变化逐条记录）。
uint64 resolved_count
# 单次检测耗时（微秒）：最近一次、历史最大。
float64 last_detection_us
float64 max_detection_us
# 按动作分类的累计解除次数。
uint64 reroute_count
uint64 backoff_count
uint64 pause_count
