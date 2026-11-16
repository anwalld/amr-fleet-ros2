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
