#pragma once

#include <string>

#include "robot_tasks/traffic_reservation.hpp"

namespace robot_tasks {

// 被 mission_runner_node 与拆出的组件共同读写的状态。
// 单线程执行器保证访问安全，不加锁。
struct MissionContext {
  std::string station_file;
  bool traffic_intersection_locks_enabled = true;
  RouteLockMap route_locks;
  RouteLockReservationMap route_locks_by_mission;
};

}  // namespace robot_tasks
