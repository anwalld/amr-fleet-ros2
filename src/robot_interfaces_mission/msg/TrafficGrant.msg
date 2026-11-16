# /traffic/grants：排队的车等到路权（event=granted），或所等路段被封需要重新规划（event=replan）。
builtin_interfaces/Time stamp
string event
string robot_id
string from_station
string to_station
string[] resource_ids
