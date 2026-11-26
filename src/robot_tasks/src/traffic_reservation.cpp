#include "robot_tasks/traffic_reservation.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace robot_tasks {
namespace {

bool StartsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

}  // namespace

std::string RouteLockId(const std::string& from, const std::string& to) {
  return from <= to ? "route_edge:" + from + "__" + to : "route_edge:" + to + "__" + from;
}

std::string RouteNodeLockId(const std::string& station_id) {
  return "route_node:" + station_id;
}

std::optional<std::pair<std::string, std::string>> ParseRouteLock(
    const std::string& resource_id) {
  const std::string prefix = "route_edge:";
  if (!StartsWith(resource_id, prefix)) {
    return std::nullopt;
  }
  const auto separator = resource_id.find("__", prefix.size());
  if (separator == std::string::npos) {
    return std::nullopt;
  }
  const auto first = resource_id.substr(prefix.size(), separator - prefix.size());
  const auto second = resource_id.substr(separator + 2);
  if (first.empty() || second.empty()) {
    return std::nullopt;
  }
  return std::make_pair(first, second);
}

bool IsTrafficBlockOwner(const std::string& owner_id) {
  return StartsWith(owner_id, "traffic_block:");
}

std::string TrafficBlockOwnerId(const std::string& reason) {
  return "traffic_block:" + reason;
}

TrafficReservationDecision PlanTrafficBlock(
    const TrafficBlockRequest& request, const RouteLockMap& route_locks) {
  TrafficReservationDecision decision;
  decision.resource_id = RouteLockId(request.from_station, request.to_station);
  if (!request.stations_known) {
    decision.message = "unknown station in route block request: " + request.from_station +
                       " -> " + request.to_station;
    return decision;
  }
  if (!request.route_exists) {
    decision.message =
        "no enabled station route to block: " + request.from_station + " -> " + request.to_station;
    return decision;
  }
  const auto existing = route_locks.find(decision.resource_id);
  if (existing != route_locks.end()) {
    decision.message =
        "route resource already reserved: " + decision.resource_id + " by " + existing->second;
    return decision;
  }
  decision.success = true;
  decision.mutate_state = true;
  decision.owner_id = TrafficBlockOwnerId(request.reason);
  decision.message = "blocked route " + request.from_station + " -> " + request.to_station;
  return decision;
}

TrafficReservationDecision PlanTrafficUnblock(
    const std::string& from_station, const std::string& to_station,
    const RouteLockMap& route_locks) {
  TrafficReservationDecision decision;
  decision.resource_id = RouteLockId(from_station, to_station);
  const auto lock = route_locks.find(decision.resource_id);
  if (lock == route_locks.end()) {
    decision.message = "route resource not found: " + decision.resource_id;
    return decision;
  }
  decision.owner_id = lock->second;
  if (!IsTrafficBlockOwner(lock->second)) {
    decision.message = "route resource is reserved by: " + lock->second;
    return decision;
  }
  decision.success = true;
  decision.mutate_state = true;
  decision.message = "unblocked route " + from_station + " -> " + to_station;
  return decision;
}

TrafficClearDecision PlanTrafficReservationClear(
    const std::string& resource_id, const RouteLockMap& route_locks) {
  TrafficClearDecision decision;
  decision.resource_id = resource_id;
  const auto lock = route_locks.find(resource_id);
  if (lock == route_locks.end()) {
    decision.message = "traffic reservation not found: " + resource_id;
    return decision;
  }
  decision.success = true;
  decision.owner_id = lock->second;
  if (IsTrafficBlockOwner(lock->second)) {
    decision.erase_resource = true;
    decision.message = "cleared blocked route " + resource_id;
    return decision;
  }
  decision.release_mission_id = lock->second;
  decision.message = "cleared mission route reservation " + resource_id;
  return decision;
}

std::vector<std::string> PausedGroupMembers(
    const std::unordered_map<std::string, std::string>& paused_group_of,
    const std::string& robot_id) {
  std::vector<std::string> members;
  const auto group = paused_group_of.find(robot_id);
  if (group != paused_group_of.end()) {
    for (const auto& [member, key] : paused_group_of) {
      if (key == group->second) {
        members.push_back(member);
      }
    }
    std::sort(members.begin(), members.end());
  }
  return members;
}

TrafficClearImpact PlanTrafficClearImpact(
    const std::string& resource_id, const RouteLockMap& route_locks,
    const std::unordered_map<std::string, std::string>& paused_group_of) {
  TrafficClearImpact impact;
  impact.decision = PlanTrafficReservationClear(resource_id, route_locks);
  if (!impact.decision.success) {
    return impact;
  }
  if (impact.decision.erase_resource) {
    impact.resource_ids.push_back(resource_id);
    return impact;
  }
  for (const auto& [id, owner] : route_locks) {
    if (owner == impact.decision.owner_id) {
      impact.resource_ids.push_back(id);
    }
  }
  std::sort(impact.resource_ids.begin(), impact.resource_ids.end());
  impact.resume_robot_ids = PausedGroupMembers(paused_group_of, impact.decision.owner_id);
  return impact;
}

bool TrafficClearImpact::Matches(
    const std::string& owner_id, const std::vector<std::string>& expected_resources,
    const std::vector<std::string>& expected_resume_robots) const {
  const auto same_members = [](const auto& actual, const auto& expected) {
    auto sorted = expected;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    return actual == sorted;
  };
  return decision.success && decision.owner_id == owner_id &&
         same_members(resource_ids, expected_resources) &&
         same_members(resume_robot_ids, expected_resume_robots);
}

void ApplyTrafficClearImpact(
    const TrafficClearImpact& impact, RouteLockMap& route_locks,
    RouteLockReservationMap& route_locks_by_mission) {
  for (const auto& id : impact.resource_ids) {
    route_locks.erase(id);
  }
  if (impact.decision.release_mission_id.empty()) {
    return;
  }
  for (auto it = route_locks_by_mission.begin(); it != route_locks_by_mission.end();) {
    const auto& owner = it->second.owner_id.empty() ? it->first : it->second.owner_id;
    if (owner == impact.decision.owner_id) {
      it = route_locks_by_mission.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<std::string> BuildRouteLockIds(
    const std::vector<std::string>& station_path, const bool include_intersection_locks) {
  std::vector<std::string> lock_ids;
  if (include_intersection_locks) {
    for (const auto& station_id : station_path) {
      lock_ids.push_back(RouteNodeLockId(station_id));
    }
  }
  for (std::size_t index = 1; index < station_path.size(); ++index) {
    lock_ids.push_back(RouteLockId(station_path[index - 1], station_path[index]));
  }
  return lock_ids;
}

bool ReserveRouteLocksForMission(
    RouteLockMap& route_locks, RouteLockReservationMap& route_locks_by_mission,
    const std::string& mission_id, const std::vector<std::string>& route_lock_ids,
    std::string* message) {
  return ReserveRouteLocksForMission(
      route_locks, route_locks_by_mission, mission_id, mission_id, route_lock_ids, message);
}

bool ReserveRouteLocksForMission(
    RouteLockMap& route_locks, RouteLockReservationMap& route_locks_by_mission,
    const std::string& mission_id, const std::string& owner_id,
    const std::vector<std::string>& route_lock_ids, std::string* message) {
  const std::string owner = owner_id.empty() ? mission_id : owner_id;
  for (const auto& lock_id : route_lock_ids) {
    const auto existing = route_locks.find(lock_id);
    if (existing != route_locks.end() && existing->second != owner) {
      if (message != nullptr) {
        *message = "route resource locked: " + lock_id + " by " + existing->second;
      }
      return false;
    }
  }
  for (const auto& lock_id : route_lock_ids) {
    route_locks[lock_id] = owner;
  }
  route_locks_by_mission[mission_id] = RouteLockReservation{mission_id, route_lock_ids, owner};
  if (message != nullptr) {
    *message = "reserved " + std::to_string(route_lock_ids.size()) + " route lock(s)";
  }
  return true;
}

bool ReleaseRouteLocksForMission(
    RouteLockMap& route_locks, RouteLockReservationMap& route_locks_by_mission,
    const std::string& mission_id) {
  bool released = false;
  std::string owner = mission_id;
  const auto reservation = route_locks_by_mission.find(mission_id);
  if (reservation != route_locks_by_mission.end()) {
    if (!reservation->second.owner_id.empty()) {
      owner = reservation->second.owner_id;
    }
    const auto lock_ids = reservation->second.route_lock_ids;
    route_locks_by_mission.erase(reservation);
    // 同一占用者的其他未结束工单还引用的资源不放。
    std::unordered_set<std::string> still_needed;
    for (const auto& [other_mission, other] : route_locks_by_mission) {
      const std::string other_owner = other.owner_id.empty() ? other_mission : other.owner_id;
      if (other_owner == owner) {
        still_needed.insert(other.route_lock_ids.begin(), other.route_lock_ids.end());
      }
    }
    for (const auto& lock_id : lock_ids) {
      const auto lock = route_locks.find(lock_id);
      if (lock != route_locks.end() && lock->second == owner && still_needed.count(lock_id) == 0U) {
        route_locks.erase(lock);
        released = true;
      }
    }
  }
  if (owner == mission_id) {
    // 占用者就是任务号（旧调用或残留）：清掉它占着的全部资源。
    for (auto lock = route_locks.begin(); lock != route_locks.end();) {
      if (lock->second == mission_id) {
        lock = route_locks.erase(lock);
        released = true;
      } else {
        ++lock;
      }
    }
  }
  return released;
}

}  // namespace robot_tasks

// ---------------------------------------------------------------------------
// M2 滚动申请 + 等待队列
// ---------------------------------------------------------------------------

namespace robot_tasks {
namespace {

bool WaiterGoesBefore(const RouteWaiter& a, const RouteWaiter& b) {
  if (a.priority != b.priority) {
    return a.priority > b.priority;
  }
  return a.sequence < b.sequence;
}

void EraseWaiter(const std::string& robot_id, RouteWaitQueueMap& wait_queues) {
  for (auto it = wait_queues.begin(); it != wait_queues.end();) {
    auto& queue = it->second;
    queue.erase(
        std::remove_if(
            queue.begin(), queue.end(),
            [&](const RouteWaiter& waiter) { return waiter.robot_id == robot_id; }),
        queue.end());
    if (queue.empty()) {
      it = wait_queues.erase(it);
    } else {
      ++it;
    }
  }
}

void InsertWaiterSorted(std::vector<RouteWaiter>& queue, const RouteWaiter& waiter) {
  const auto position = std::find_if(
      queue.begin(), queue.end(),
      [&](const RouteWaiter& existing) { return WaiterGoesBefore(waiter, existing); });
  queue.insert(position, waiter);
}

// 整组资源是否都空闲或已归该车所有。
bool ResourcesAvailableFor(
    const std::string& robot_id, const std::vector<std::string>& resource_ids,
    const RouteLockMap& route_locks) {
  for (const auto& resource_id : resource_ids) {
    const auto lock = route_locks.find(resource_id);
    if (lock != route_locks.end() && lock->second != robot_id) {
      return false;
    }
  }
  return true;
}

std::string JoinResourceIds(const std::vector<std::string>& ids) {
  std::string joined;
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (index > 0) {
      joined += ",";
    }
    joined += ids[index];
  }
  return joined;
}

}  // namespace

std::vector<std::string> BuildRouteSegmentResourceIds(
    const std::string& from_station, const std::string& to_station,
    const bool include_intersection_locks) {
  std::vector<std::string> resource_ids;
  if (!from_station.empty() && from_station != to_station) {
    resource_ids.push_back(RouteLockId(from_station, to_station));
  }
  if (include_intersection_locks && !to_station.empty()) {
    resource_ids.push_back(RouteNodeLockId(to_station));
  }
  return resource_ids;
}

std::vector<std::string> BuildRoutePathResourceIds(
    const std::vector<std::string>& station_path, const bool include_intersection_locks) {
  std::vector<std::string> resource_ids;
  for (std::size_t index = 1; index < station_path.size(); ++index) {
    resource_ids.push_back(RouteLockId(station_path[index - 1], station_path[index]));
  }
  if (include_intersection_locks) {
    for (std::size_t index = 1; index < station_path.size(); ++index) {
      resource_ids.push_back(RouteNodeLockId(station_path[index]));
    }
  }
  return resource_ids;
}

RouteSegmentDecision PlanRouteSegmentRequest(
    const RouteSegmentRequest& request, RouteLockMap& route_locks,
    RouteWaitQueueMap& wait_queues, std::uint64_t* next_sequence, const RouteHoldMap* holds) {
  RouteSegmentDecision decision;
  decision.resource_ids = request.resource_ids;
  if (request.robot_id.empty()) {
    decision.message = "route segment request requires a robot_id";
    return decision;
  }

  for (const auto& resource_id : request.resource_ids) {
    const auto lock = route_locks.find(resource_id);
    if (lock == route_locks.end()) {
      // 空闲但暂扣给了别的（优先级不低于本次申请的）车：同样算阻塞。
      if (holds != nullptr) {
        const auto hold = holds->find(resource_id);
        if (hold != holds->end() && hold->second.robot_id != request.robot_id &&
            hold->second.priority >= request.priority) {
          decision.blocking_resource_ids.push_back(resource_id);
          decision.blocking_owner_ids.push_back(hold->second.robot_id);
        }
      }
      continue;
    }
    if (lock->second == request.robot_id) {
      continue;
    }
    decision.blocking_resource_ids.push_back(resource_id);
    decision.blocking_owner_ids.push_back(lock->second);
    if (IsTrafficBlockOwner(lock->second)) {
      decision.replan_required = true;
    }
  }

  if (decision.blocking_resource_ids.empty()) {
    for (const auto& resource_id : request.resource_ids) {
      route_locks[resource_id] = request.robot_id;
    }
    EraseWaiter(request.robot_id, wait_queues);
    decision.granted = true;
    decision.message = "granted " + request.robot_id + " " +
                       request.from_station + " -> " + request.to_station + ": " +
                       JoinResourceIds(request.resource_ids);
    return decision;
  }

  const std::string blockers = JoinResourceIds(decision.blocking_resource_ids) + " held by " +
                               JoinResourceIds(decision.blocking_owner_ids);
  if (decision.replan_required) {
    EraseWaiter(request.robot_id, wait_queues);
    decision.message = "route blocked, replan required: " + blockers;
    return decision;
  }
  if (!request.allow_queue) {
    decision.message = "route resource locked: " + blockers;
    return decision;
  }

  // 同一辆车只等一组资源：先撤掉旧的等待，再按优先级/到达顺序排进每个缺的资源。
  const auto previous = FindRouteWaiter(request.robot_id, wait_queues);
  EraseWaiter(request.robot_id, wait_queues);
  RouteWaiter waiter;
  waiter.robot_id = request.robot_id;
  waiter.from_station = request.from_station;
  waiter.to_station = request.to_station;
  waiter.priority = request.priority;
  waiter.resource_ids = request.resource_ids;
  waiter.goal_station = request.goal_station;
  if (previous.has_value() && previous->resource_ids == request.resource_ids &&
      previous->priority == request.priority) {
    waiter.sequence = previous->sequence;  // 重复申请保留原来的排位
    waiter.since_s = previous->since_s;
  } else {
    waiter.sequence = next_sequence != nullptr ? (*next_sequence)++ : 0;
    waiter.since_s = request.now_s;
    decision.newly_queued = true;
  }
  for (const auto& resource_id : decision.blocking_resource_ids) {
    InsertWaiterSorted(wait_queues[resource_id], waiter);
  }
  decision.queued = true;
  decision.message = "queued " + request.robot_id + " for " + blockers;
  return decision;
}

RouteSegmentReleaseDecision PlanRouteSegmentRelease(
    const std::string& robot_id, const std::vector<std::string>& resource_ids,
    RouteLockMap& route_locks, RouteWaitQueueMap& wait_queues) {
  RouteSegmentReleaseDecision decision;
  std::vector<std::string> candidates = resource_ids;
  if (candidates.empty()) {
    candidates = RouteResourcesOwnedBy(robot_id, route_locks);
  }
  for (const auto& resource_id : candidates) {
    const auto lock = route_locks.find(resource_id);
    if (lock != route_locks.end() && lock->second == robot_id) {
      route_locks.erase(lock);
      decision.released_resource_ids.push_back(resource_id);
    }
  }

  decision.granted =
      PlanRouteWaitGrants(decision.released_resource_ids, route_locks, wait_queues);

  decision.message = "released " + std::to_string(decision.released_resource_ids.size()) +
                     " resource(s) of " + robot_id + ", granted " +
                     std::to_string(decision.granted.size()) + " waiter(s)";
  return decision;
}

std::vector<RouteGrant> PlanRouteWaitGrants(
    const std::vector<std::string>& candidate_resource_ids, RouteLockMap& route_locks,
    RouteWaitQueueMap& wait_queues) {
  return PlanRouteWaitGrants(
      candidate_resource_ids, route_locks, wait_queues, RouteWaitGrantOptions{}, nullptr);
}

std::vector<RouteGrant> PlanRouteWaitGrants(
    const std::vector<std::string>& candidate_resource_ids, RouteLockMap& route_locks,
    RouteWaitQueueMap& wait_queues, const RouteWaitGrantOptions& options, RouteHoldMap* holds) {
  // 全部等待者按优先级、到达顺序统一排序后依次过一遍，保证先到的车不会因为资源遍历
  // 顺序被后到的车插队；整组资源拿不齐的跳过。只有候选等待者会被授予，
  // 但所有等待者都参与防饿死的暂扣（暂扣是全局的）。
  std::vector<RouteWaiter> waiters;
  std::unordered_set<std::string> seen;
  for (const auto& [resource_id, queue] : wait_queues) {
    for (const auto& waiter : queue) {
      if (seen.insert(waiter.robot_id).second) {
        waiters.push_back(waiter);
      }
    }
  }
  std::sort(waiters.begin(), waiters.end(), WaiterGoesBefore);

  std::unordered_set<std::string> candidates;
  const bool all_candidates = candidate_resource_ids.empty();
  for (const auto& resource_id : candidate_resource_ids) {
    const auto queue_it = wait_queues.find(resource_id);
    if (queue_it != wait_queues.end()) {
      for (const auto& waiter : queue_it->second) {
        candidates.insert(waiter.robot_id);
      }
    }
  }

  const auto frozen = [&](const std::string& robot_id) {
    return options.frozen_robot_ids != nullptr && options.frozen_robot_ids->count(robot_id) != 0U;
  };

  RouteHoldMap new_holds;
  std::vector<RouteGrant> granted;
  for (const auto& waiter : waiters) {
    if (frozen(waiter.robot_id)) {
      continue;
    }
    bool available = ResourcesAvailableFor(waiter.robot_id, waiter.resource_ids, route_locks);
    if (available) {
      for (const auto& resource_id : waiter.resource_ids) {
        const auto hold = new_holds.find(resource_id);
        if (hold != new_holds.end() && hold->second.robot_id != waiter.robot_id) {
          available = false;
          break;
        }
      }
    }
    if (available && (all_candidates || candidates.count(waiter.robot_id) != 0U)) {
      for (const auto& granted_id : waiter.resource_ids) {
        route_locks[granted_id] = waiter.robot_id;
      }
      EraseWaiter(waiter.robot_id, wait_queues);
      granted.push_back(RouteGrant{
          waiter.robot_id, waiter.from_station, waiter.to_station, waiter.resource_ids});
      continue;
    }
    const bool starving =
        options.starvation_s > 0.0 && options.now_s - waiter.since_s >= options.starvation_s;
    if (!available && starving) {
      for (const auto& resource_id : waiter.resource_ids) {
        if (route_locks.count(resource_id) == 0U && new_holds.count(resource_id) == 0U) {
          new_holds[resource_id] = RouteHold{waiter.robot_id, waiter.priority};
        }
      }
    }
  }
  if (holds != nullptr) {
    *holds = std::move(new_holds);
  }
  return granted;
}

std::optional<RouteWaiter> WithdrawRouteWaits(
    const std::string& robot_id, RouteWaitQueueMap& wait_queues) {
  const auto waiter = FindRouteWaiter(robot_id, wait_queues);
  EraseWaiter(robot_id, wait_queues);
  return waiter;
}

std::vector<RouteWaiter> PlanRouteBlockWaiterReplan(
    const std::string& resource_id, RouteWaitQueueMap& wait_queues) {
  // 按"要申请的整组资源"找，不能只看被封路段自己的队列：封路只允许封空闲路段，
  // 而车只排在被占的资源上，那条队列在运行中总是空的。
  std::vector<RouteWaiter> affected;
  for (const auto& [queued_on, queue] : wait_queues) {
    for (const auto& waiter : queue) {
      const bool needs_blocked =
          queued_on == resource_id ||
          std::find(waiter.resource_ids.begin(), waiter.resource_ids.end(), resource_id) !=
              waiter.resource_ids.end();
      const bool seen = std::any_of(affected.begin(), affected.end(), [&](const RouteWaiter& w) {
        return w.robot_id == waiter.robot_id;
      });
      if (needs_blocked && !seen) {
        affected.push_back(waiter);
      }
    }
  }
  std::sort(affected.begin(), affected.end(), [](const RouteWaiter& a, const RouteWaiter& b) {
    return a.priority != b.priority ? a.priority > b.priority : a.sequence < b.sequence;
  });
  for (const auto& waiter : affected) {
    EraseWaiter(waiter.robot_id, wait_queues);
  }
  return affected;
}

std::optional<RouteWaiter> FindRouteWaiter(
    const std::string& robot_id, const RouteWaitQueueMap& wait_queues) {
  for (const auto& [resource_id, queue] : wait_queues) {
    for (const auto& waiter : queue) {
      if (waiter.robot_id == robot_id) {
        return waiter;
      }
    }
  }
  return std::nullopt;
}

std::vector<std::string> RouteResourcesOwnedBy(
    const std::string& robot_id, const RouteLockMap& route_locks) {
  std::vector<std::string> owned;
  for (const auto& [resource_id, owner_id] : route_locks) {
    if (owner_id == robot_id) {
      owned.push_back(resource_id);
    }
  }
  std::sort(owned.begin(), owned.end());
  return owned;
}

}  // namespace robot_tasks
