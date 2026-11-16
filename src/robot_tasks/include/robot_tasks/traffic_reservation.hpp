#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace robot_tasks {

struct RouteLockReservation {
  std::string mission_id;
  std::vector<std::string> route_lock_ids;
};

using RouteLockMap = std::unordered_map<std::string, std::string>;
using RouteLockReservationMap = std::unordered_map<std::string, RouteLockReservation>;

struct TrafficBlockRequest {
  std::string from_station;
  std::string to_station;
  std::string reason;
  bool stations_known = false;
  bool route_exists = false;
};

struct TrafficReservationDecision {
  bool success = false;
  bool mutate_state = false;
  std::string resource_id;
  std::string owner_id;
  std::string message;
};

struct TrafficClearDecision {
  bool success = false;
  bool erase_resource = false;
  std::string resource_id;
  std::string owner_id;
  std::string release_mission_id;
  std::string message;
};

struct TrafficDeadlockDecision {
  bool deadlocked = false;
  std::vector<std::string> mission_ids;
  std::vector<std::string> conflict_resource_ids;
  std::vector<std::string> conflict_station_ids;
  std::vector<std::string> descriptions;
  std::string message = "no traffic deadlock detected";
};

// ---------------------------------------------------------------------------
// M2 滚动申请 + 等待队列
//
// 占用者是车辆号（不再是任务号）。车在站点 A、下一站 B 时申请“路段 A–B + 路口 B”
// 一组资源；申请不到不拒绝，而是进入每个拿不到的资源的等待队列，按优先级（大的先）、
// 再按到达顺序排。资源释放时沿队列找第一个整组资源都能拿到的等待者授予。
// 封路的资源不发放，等在上面的车被移出队列并标记需要重新规划。
//
// 这些函数直接改传入的路权表和等待队列，不依赖 ROS，便于单元测试。
// ---------------------------------------------------------------------------

struct RouteWaiter {
  std::string robot_id;
  std::string from_station;
  std::string to_station;
  int priority = 0;
  std::uint64_t sequence = 0;
  std::vector<std::string> resource_ids;
};

// 资源 → 等待者（已按优先级降序、到达顺序升序排好）。
using RouteWaitQueueMap = std::unordered_map<std::string, std::vector<RouteWaiter>>;

struct RouteSegmentRequest {
  std::string robot_id;
  std::string from_station;
  std::string to_station;
  std::vector<std::string> resource_ids;
  int priority = 0;
  // false：整条路径一次锁完，拿不到就拒绝（whole_route 对照组），不进队列。
  bool allow_queue = true;
};

struct RouteSegmentDecision {
  bool granted = false;
  bool queued = false;
  // 被封路挡住：不授予也不排队，申请方应重新规划。
  bool replan_required = false;
  std::vector<std::string> resource_ids;
  std::vector<std::string> blocking_resource_ids;
  std::vector<std::string> blocking_owner_ids;
  std::string message;
};

struct RouteGrant {
  std::string robot_id;
  std::string from_station;
  std::string to_station;
  std::vector<std::string> resource_ids;
};

struct RouteSegmentReleaseDecision {
  std::vector<std::string> released_resource_ids;
  std::vector<RouteGrant> granted;
  std::string message;
};

std::string RouteLockId(const std::string& from, const std::string& to);
std::string RouteNodeLockId(const std::string& station_id);
std::optional<std::pair<std::string, std::string>> ParseRouteLock(
    const std::string& resource_id);
bool IsTrafficBlockOwner(const std::string& owner_id);
std::string TrafficBlockOwnerId(const std::string& reason);
TrafficReservationDecision PlanTrafficBlock(
    const TrafficBlockRequest& request, const RouteLockMap& route_locks);
TrafficReservationDecision PlanTrafficUnblock(
    const std::string& from_station, const std::string& to_station,
    const RouteLockMap& route_locks);
TrafficClearDecision PlanTrafficReservationClear(
    const std::string& resource_id, const RouteLockMap& route_locks);
TrafficDeadlockDecision PlanTrafficDeadlockDetection(const RouteLockMap& route_locks);
std::vector<std::string> BuildRouteLockIds(
    const std::vector<std::string>& station_path, bool include_intersection_locks);
bool ReserveRouteLocksForMission(
    RouteLockMap& route_locks, RouteLockReservationMap& route_locks_by_mission,
    const std::string& mission_id, const std::vector<std::string>& route_lock_ids,
    std::string* message);
bool ReleaseRouteLocksForMission(
    RouteLockMap& route_locks, RouteLockReservationMap& route_locks_by_mission,
    const std::string& mission_id);

// 一段路（from → to）要申请的资源：路段锁 + 下一个路口锁（可关）。
// from 为空表示“直接去 to”（起点未知），只申请路口 to。
std::vector<std::string> BuildRouteSegmentResourceIds(
    const std::string& from_station, const std::string& to_station,
    bool include_intersection_locks);

// 申请一段路。整组资源都空闲或已归该车所有 → 授予并锁上；否则按 allow_queue 排队或拒绝。
// 同一辆车同一时刻只等一组资源：新申请会替换它先前的等待。
// *next_sequence 用来给新等待者编号，调用后加一。
RouteSegmentDecision PlanRouteSegmentRequest(
    const RouteSegmentRequest& request, RouteLockMap& route_locks,
    RouteWaitQueueMap& wait_queues, std::uint64_t* next_sequence);

// 释放该车持有的资源（resource_ids 为空 = 全部）。等在这些资源上的车按优先级、
// 到达顺序依次尝试授予，整组资源拿不齐的跳过（可能授予多辆车）。
RouteSegmentReleaseDecision PlanRouteSegmentRelease(
    const std::string& robot_id, const std::vector<std::string>& resource_ids,
    RouteLockMap& route_locks, RouteWaitQueueMap& wait_queues);

// 撤掉一辆车的全部等待（取消任务时用）。返回撤掉的等待者（若有）。
std::optional<RouteWaiter> WithdrawRouteWaits(
    const std::string& robot_id, RouteWaitQueueMap& wait_queues);

// 封路后：等在该资源上的车全部移出队列，由调用方通知它们重新规划。
std::vector<RouteWaiter> PlanRouteBlockWaiterReplan(
    const std::string& resource_id, RouteWaitQueueMap& wait_queues);

// 一辆车当前等待的那组资源（没有则 nullopt）。
std::optional<RouteWaiter> FindRouteWaiter(
    const std::string& robot_id, const RouteWaitQueueMap& wait_queues);

// 该车持有的全部资源（排序后）。
std::vector<std::string> RouteResourcesOwnedBy(
    const std::string& robot_id, const RouteLockMap& route_locks);

}  // namespace robot_tasks
