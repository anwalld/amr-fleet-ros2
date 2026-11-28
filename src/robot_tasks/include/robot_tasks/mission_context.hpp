#pragma once

#include <cstdint>
#include <string>

#include "robot_tasks/traffic_deadlock.hpp"
#include "robot_tasks/traffic_reservation.hpp"

namespace robot_tasks {

// 被 mission_runner_node 与拆出的组件共同读写的状态。
// 单线程执行器保证访问安全，不加锁。
struct MissionContext {
  std::string station_file;
  bool traffic_intersection_locks_enabled = true;
  RouteLockMap route_locks;
  RouteLockRevisionMap route_lock_revisions;
  RouteLockReservationMap route_locks_by_mission;
  std::uint64_t traffic_service_epoch = 0;
  std::uint64_t next_route_lock_revision = 0;
  // M2：路权模式。whole_route = 入队时锁整条路径、拿不到就拒绝（旧行为，对照组）；
  // rolling = 边走边申请前方路段、拿不到排队（默认）。
  std::string traffic_reservation_mode = "rolling";
  // 各资源的等待队列（rolling 模式）与等待者编号。
  RouteWaitQueueMap route_wait_queues;
  std::uint64_t next_route_wait_sequence = 1;
  // M3：防饿死暂扣表（每次补授予重算）与 whole_route 被拒申请的重试等待（计入等待图）。
  RouteHoldMap route_holds;
  WholeRouteRetryWaitMap whole_route_retry_waits;
};

}  // namespace robot_tasks
