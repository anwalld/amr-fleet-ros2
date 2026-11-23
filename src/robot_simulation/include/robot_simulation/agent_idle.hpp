#pragma once

#include <string>

namespace robot_simulation {

// 车队代理空闲时是否驶回停车位。
// 至少完成过一个任务才回（acceptance-suite-rework，2026-11-19）：刚启动、还没接过任务的车原地等待。
// 否则给了 home_stations 的车一启动就同时往停车位开，会在场景布置好之前自己堵成环
// （夜跑 #1 阶段 6 由 9/9 掉到 1/9 的原因）。
inline bool ShouldReturnHomeWhenIdle(
    bool return_home_when_idle, bool has_task, bool idle, bool completed_any_task,
    const std::string& current_station, const std::string& home_station) {
  return return_home_when_idle && !has_task && idle && completed_any_task &&
         current_station != home_station;
}

}  // namespace robot_simulation
