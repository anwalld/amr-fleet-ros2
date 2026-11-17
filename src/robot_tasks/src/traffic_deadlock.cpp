#include "robot_tasks/traffic_deadlock.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <unordered_set>
#include <utility>

namespace robot_tasks {
namespace {

std::string JoinIds(const std::vector<std::string>& ids, const std::string& separator) {
  std::string joined;
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (index > 0) {
      joined += separator;
    }
    joined += ids[index];
  }
  return joined;
}

void SortUnique(std::vector<std::string>& ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

// 资源涉及的站点：路段锁两端、路口锁本站。
std::vector<std::string> ResourceStations(const std::string& resource_id) {
  if (const auto edge = ParseRouteLock(resource_id)) {
    return {edge->first, edge->second};
  }
  const std::string node_prefix = "route_node:";
  if (resource_id.rfind(node_prefix, 0) == 0 && resource_id.size() > node_prefix.size()) {
    return {resource_id.substr(node_prefix.size())};
  }
  return {};
}

}  // namespace

std::string WaitCycleKey(const WaitCycle& cycle) {
  std::vector<std::string> robots = cycle.robot_ids;
  std::sort(robots.begin(), robots.end());
  return JoinIds(robots, ",");
}

std::string DescribeWaitCycle(const WaitCycle& cycle) {
  if (cycle.robot_ids.empty()) {
    return "";
  }
  std::string text = cycle.robot_ids.front();
  for (std::size_t index = 0; index < cycle.robot_ids.size(); ++index) {
    const auto& next = cycle.robot_ids[(index + 1) % cycle.robot_ids.size()];
    const std::string via =
        index < cycle.edges.size() ? JoinIds(cycle.edges[index].resource_ids, ",") : "";
    text += " -[" + via + "]-> " + next;
  }
  return text;
}

void PruneWholeRouteRetryWaits(WholeRouteRetryWaitMap& retry_waits, const double now_s) {
  for (auto it = retry_waits.begin(); it != retry_waits.end();) {
    if (it->second.expires_s <= now_s) {
      it = retry_waits.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<WaitGraphEdge> BuildWaitGraph(
    const RouteLockMap& route_locks, const RouteWaitQueueMap& wait_queues,
    const WholeRouteRetryWaitMap& retry_waits, const RouteHoldMap& holds, const double now_s) {
  std::map<std::pair<std::string, std::string>, std::set<std::string>> merged;
  const auto add_wait = [&](const std::string& waiter, const std::string& resource_id) {
    const auto lock = route_locks.find(resource_id);
    if (lock != route_locks.end()) {
      if (lock->second != waiter && !IsTrafficBlockOwner(lock->second)) {
        merged[{waiter, lock->second}].insert(resource_id);
      }
      return;
    }
    const auto hold = holds.find(resource_id);
    if (hold != holds.end() && hold->second.robot_id != waiter) {
      merged[{waiter, hold->second.robot_id}].insert(resource_id);
    }
  };

  std::unordered_set<std::string> seen;
  for (const auto& [resource_id, queue] : wait_queues) {
    for (const auto& waiter : queue) {
      if (!seen.insert(waiter.robot_id).second) {
        continue;
      }
      for (const auto& wanted : waiter.resource_ids) {
        add_wait(waiter.robot_id, wanted);
      }
    }
  }
  for (const auto& [robot_id, retry] : retry_waits) {
    if (retry.expires_s <= now_s || seen.count(robot_id) != 0U) {
      continue;
    }
    for (const auto& wanted : retry.blocking_resource_ids) {
      add_wait(robot_id, wanted);
    }
  }

  std::vector<WaitGraphEdge> edges;
  for (const auto& [pair, resources] : merged) {
    edges.push_back(
        WaitGraphEdge{pair.first, pair.second, std::vector<std::string>(resources.begin(), resources.end())});
  }
  return edges;
}

std::vector<WaitCycle> FindWaitCycles(const std::vector<WaitGraphEdge>& edges) {
  // 邻接表（std::map 保证遍历顺序确定）。
  std::map<std::string, std::vector<const WaitGraphEdge*>> adjacency;
  for (const auto& edge : edges) {
    adjacency[edge.from_robot_id].push_back(&edge);
    adjacency.try_emplace(edge.to_robot_id);
  }

  std::vector<WaitCycle> cycles;
  std::unordered_set<std::string> used;
  while (true) {
    std::unordered_map<std::string, int> color;  // 0 白 1 灰 2 黑
    std::vector<const WaitGraphEdge*> path;      // 当前 DFS 路径上的边
    std::vector<std::string> stack;              // 当前 DFS 路径上的车
    std::optional<WaitCycle> found;

    std::function<void(const std::string&)> visit = [&](const std::string& robot) {
      color[robot] = 1;
      stack.push_back(robot);
      for (const auto* edge : adjacency[robot]) {
        if (found.has_value()) {
          break;
        }
        const auto& next = edge->to_robot_id;
        if (used.count(next) != 0U) {
          continue;
        }
        if (color[next] == 1) {
          // 回边：栈中从 next 到 robot 的这一段加上本条边就是环。
          const auto begin = std::find(stack.begin(), stack.end(), next);
          const auto offset = static_cast<std::size_t>(std::distance(stack.begin(), begin));
          WaitCycle cycle;
          cycle.robot_ids.assign(begin, stack.end());
          for (std::size_t index = offset; index < path.size(); ++index) {
            cycle.edges.push_back(*path[index]);
          }
          cycle.edges.push_back(*edge);
          found = std::move(cycle);
          break;
        }
        if (color[next] == 0) {
          path.push_back(edge);
          visit(next);
          path.pop_back();
        }
      }
      stack.pop_back();
      color[robot] = 2;
    };

    for (const auto& [robot, outgoing] : adjacency) {
      if (found.has_value()) {
        break;
      }
      if (used.count(robot) == 0U && color[robot] == 0) {
        visit(robot);
      }
    }
    if (!found.has_value()) {
      break;
    }

    // 规范化：从字典序最小的车开始。
    auto& cycle = *found;
    const auto min_it = std::min_element(cycle.robot_ids.begin(), cycle.robot_ids.end());
    const auto shift = static_cast<std::ptrdiff_t>(std::distance(cycle.robot_ids.begin(), min_it));
    std::rotate(cycle.robot_ids.begin(), cycle.robot_ids.begin() + shift, cycle.robot_ids.end());
    std::rotate(cycle.edges.begin(), cycle.edges.begin() + shift, cycle.edges.end());
    for (const auto& edge : cycle.edges) {
      cycle.resource_ids.insert(cycle.resource_ids.end(), edge.resource_ids.begin(), edge.resource_ids.end());
    }
    SortUnique(cycle.resource_ids);
    used.insert(cycle.robot_ids.begin(), cycle.robot_ids.end());
    cycles.push_back(std::move(cycle));
  }
  return cycles;
}

TrafficDeadlockDecision BuildTrafficDeadlockDecision(const std::vector<WaitCycle>& cycles) {
  TrafficDeadlockDecision decision;
  for (const auto& cycle : cycles) {
    decision.mission_ids.insert(decision.mission_ids.end(), cycle.robot_ids.begin(), cycle.robot_ids.end());
    decision.conflict_resource_ids.insert(
        decision.conflict_resource_ids.end(), cycle.resource_ids.begin(), cycle.resource_ids.end());
    for (const auto& resource_id : cycle.resource_ids) {
      const auto stations = ResourceStations(resource_id);
      decision.conflict_station_ids.insert(
          decision.conflict_station_ids.end(), stations.begin(), stations.end());
    }
    decision.descriptions.push_back(DescribeWaitCycle(cycle));
  }
  SortUnique(decision.mission_ids);
  SortUnique(decision.conflict_resource_ids);
  SortUnique(decision.conflict_station_ids);
  decision.deadlocked = !cycles.empty();
  if (decision.deadlocked) {
    decision.message =
        "traffic deadlock detected: " + std::to_string(cycles.size()) + " wait cycle(s)";
  }
  return decision;
}

TrafficDeadlockDecision PlanTrafficDeadlockDetection(
    const RouteLockMap& route_locks, const RouteWaitQueueMap& wait_queues,
    const WholeRouteRetryWaitMap& retry_waits, const RouteHoldMap& holds, const double now_s) {
  return BuildTrafficDeadlockDecision(
      FindWaitCycles(BuildWaitGraph(route_locks, wait_queues, retry_waits, holds, now_s)));
}

std::optional<TrafficWaitInfo> FindTrafficWaitInfo(
    const std::string& robot_id, const RouteWaitQueueMap& wait_queues,
    const WholeRouteRetryWaitMap& retry_waits, const double now_s) {
  if (const auto waiter = FindRouteWaiter(robot_id, wait_queues)) {
    return TrafficWaitInfo{robot_id, waiter->priority, waiter->from_station, waiter->goal_station};
  }
  const auto retry = retry_waits.find(robot_id);
  if (retry != retry_waits.end() && retry->second.expires_s > now_s) {
    return TrafficWaitInfo{
        robot_id, retry->second.priority, retry->second.from_station, retry->second.goal_station};
  }
  return std::nullopt;
}

std::optional<std::vector<std::string>> FindRoutePathAvoiding(
    const StationCatalog& catalog, const std::string& from, const std::string& to,
    const std::vector<std::string>& avoid_resource_ids, double* distance_m) {
  if (FindStation(catalog, from) == nullptr || FindStation(catalog, to) == nullptr) {
    return std::nullopt;
  }
  const std::unordered_set<std::string> avoid(avoid_resource_ids.begin(), avoid_resource_ids.end());
  const auto node_blocked = [&](const std::string& station) {
    return station != from && avoid.count(RouteNodeLockId(station)) != 0U;
  };
  if (from == to) {
    if (distance_m != nullptr) {
      *distance_m = 0.0;
    }
    return std::vector<std::string>{from};
  }

  // 有向邻接表。
  std::map<std::string, std::vector<std::pair<std::string, double>>> adjacency;
  for (const auto& edge : catalog.edges) {
    if (!edge.enabled || avoid.count(RouteLockId(edge.from, edge.to)) != 0U) {
      continue;
    }
    adjacency[edge.from].emplace_back(edge.to, edge.distance_m);
    if (edge.bidirectional) {
      adjacency[edge.to].emplace_back(edge.from, edge.distance_m);
    }
  }

  std::map<std::string, double> distance;
  std::map<std::string, std::string> previous;
  std::set<std::pair<double, std::string>> frontier;
  distance[from] = 0.0;
  frontier.insert({0.0, from});
  while (!frontier.empty()) {
    const auto [dist, station] = *frontier.begin();
    frontier.erase(frontier.begin());
    if (station == to) {
      break;
    }
    for (const auto& [next, length] : adjacency[station]) {
      if (node_blocked(next)) {
        continue;
      }
      const double candidate = dist + length;
      const auto known = distance.find(next);
      if (known == distance.end() || candidate < known->second) {
        if (known != distance.end()) {
          frontier.erase({known->second, next});
        }
        distance[next] = candidate;
        previous[next] = station;
        frontier.insert({candidate, next});
      }
    }
  }
  const auto reached = distance.find(to);
  if (reached == distance.end() || !std::isfinite(reached->second)) {
    return std::nullopt;
  }
  std::vector<std::string> path{to};
  while (path.back() != from) {
    path.push_back(previous.at(path.back()));
  }
  std::reverse(path.begin(), path.end());
  if (distance_m != nullptr) {
    *distance_m = reached->second;
  }
  return path;
}

std::string SelectYieldRobot(
    const WaitCycle& cycle, const std::unordered_map<std::string, TrafficWaitInfo>& info,
    const StationCatalog& catalog) {
  struct Candidate {
    std::string robot_id;
    int priority = 0;
    double remaining_m = 0.0;
  };
  std::vector<Candidate> candidates;
  for (const auto& robot_id : cycle.robot_ids) {
    Candidate candidate{robot_id, 0, 0.0};
    const auto it = info.find(robot_id);
    if (it != info.end()) {
      candidate.priority = it->second.priority;
      double remaining = 0.0;
      if (!it->second.goal_station.empty() &&
          FindRoutePathAvoiding(catalog, it->second.current_station, it->second.goal_station, {}, &remaining)) {
        candidate.remaining_m = remaining;
      }
    }
    candidates.push_back(candidate);
  }
  if (candidates.empty()) {
    return "";
  }
  // "更该让路"排在前面：优先级低 → 剩余路程长 → 车辆号大。
  const auto yields_first = [](const Candidate& a, const Candidate& b) {
    if (a.priority != b.priority) {
      return a.priority < b.priority;
    }
    if (std::abs(a.remaining_m - b.remaining_m) > 1e-6) {
      return a.remaining_m > b.remaining_m;
    }
    return a.robot_id > b.robot_id;
  };
  return std::min_element(candidates.begin(), candidates.end(), yields_first)->robot_id;
}

DeadlockYieldPlan PlanDeadlockYield(
    const WaitCycle& cycle, const TrafficWaitInfo& yielder, const StationCatalog& catalog,
    const RouteLockMap& route_locks, const RouteHoldMap& holds, const double hold_s) {
  DeadlockYieldPlan plan;
  plan.robot_id = yielder.robot_id;
  plan.cycle_robot_ids = cycle.robot_ids;

  // 应绕开：环上他车占用或暂扣的资源，以及全部封路。
  const std::unordered_set<std::string> others(cycle.robot_ids.begin(), cycle.robot_ids.end());
  for (const auto& [resource_id, owner_id] : route_locks) {
    if (IsTrafficBlockOwner(owner_id) ||
        (owner_id != yielder.robot_id && others.count(owner_id) != 0U)) {
      plan.avoid_resource_ids.push_back(resource_id);
    }
  }
  for (const auto& [resource_id, hold] : holds) {
    if (hold.robot_id != yielder.robot_id && others.count(hold.robot_id) != 0U) {
      plan.avoid_resource_ids.push_back(resource_id);
    }
  }
  SortUnique(plan.avoid_resource_ids);

  // 1. 改道。
  if (!yielder.goal_station.empty() && yielder.goal_station != yielder.current_station) {
    if (auto path = FindRoutePathAvoiding(
            catalog, yielder.current_station, yielder.goal_station, plan.avoid_resource_ids)) {
      plan.action = "reroute";
      plan.station_path = std::move(*path);
      plan.message = "reroute " + yielder.robot_id + " to " + yielder.goal_station + " via " +
                     JoinIds(plan.station_path, ",");
      return plan;
    }
  }

  // 2. 退避到最近的空闲避让点。
  double best = std::numeric_limits<double>::infinity();
  for (const auto& station : catalog.stations) {
    if (!IsYieldPoint(station) || station.id == yielder.current_station) {
      continue;
    }
    const auto node = RouteNodeLockId(station.id);
    const auto lock = route_locks.find(node);
    if (lock != route_locks.end() && lock->second != yielder.robot_id) {
      continue;
    }
    const auto hold = holds.find(node);
    if (hold != holds.end() && hold->second.robot_id != yielder.robot_id) {
      continue;
    }
    double distance = 0.0;
    auto path = FindRoutePathAvoiding(
        catalog, yielder.current_station, station.id, plan.avoid_resource_ids, &distance);
    if (!path.has_value()) {
      continue;
    }
    const bool closer = distance < best - 1e-9;
    const bool tie_smaller_id = std::abs(distance - best) <= 1e-9 && !plan.station_path.empty() &&
                                station.id < plan.station_path.back();
    if (closer || tie_smaller_id) {
      best = distance;
      plan.station_path = std::move(*path);
    }
  }
  if (!plan.station_path.empty()) {
    plan.action = "backoff";
    plan.hold_s = hold_s;
    plan.message = "backoff " + yielder.robot_id + " to " + plan.station_path.back() + " via " +
                   JoinIds(plan.station_path, ",");
    return plan;
  }

  plan.action = "pause";
  plan.message = "no reroute or free yield point for " + yielder.robot_id;
  return plan;
}

int RecordDeadlockOccurrence(
    DeadlockHistory& history, const std::string& cycle_key, const double now_s,
    const double window_s) {
  auto& times = history.occurrences_s[cycle_key];
  times.erase(
      std::remove_if(times.begin(), times.end(), [&](double t) { return now_s - t > window_s; }),
      times.end());
  times.push_back(now_s);
  return static_cast<int>(times.size());
}

DeadlockTrackUpdate TrackDeadlocks(
    ActiveDeadlockMap& active, const std::vector<WaitCycle>& current, const double now_s,
    const double yield_timeout_s) {
  DeadlockTrackUpdate update;
  std::unordered_set<std::string> current_keys;
  for (const auto& cycle : current) {
    const auto key = WaitCycleKey(cycle);
    current_keys.insert(key);
    const auto it = active.find(key);
    if (it == active.end()) {
      update.new_cycles.push_back(cycle);
      continue;
    }
    const bool yielded = it->second.action == "reroute" || it->second.action == "backoff";
    if (yielded && yield_timeout_s > 0.0 && now_s - it->second.detected_s > yield_timeout_s) {
      // 让路车没按指令动（或又绕回来了）：当作新的一次成环重新处理。
      active.erase(it);
      update.new_cycles.push_back(cycle);
    }
  }
  for (auto it = active.begin(); it != active.end();) {
    if (current_keys.count(it->first) == 0U) {
      update.resolved.push_back(
          ResolvedDeadlock{it->second, (now_s - it->second.detected_s) * 1000.0});
      it = active.erase(it);
    } else {
      ++it;
    }
  }
  std::sort(update.resolved.begin(), update.resolved.end(), [](const auto& a, const auto& b) {
    return a.deadlock.detected_s < b.deadlock.detected_s;
  });
  return update;
}

}  // namespace robot_tasks
