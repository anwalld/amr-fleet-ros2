#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace robot_simulation {

enum class RouteEntryAction { kContinue, kWaitPaused, kWaitInFlight, kApplyYield };

inline RouteEntryAction DecideRouteEntryAction(
    const bool paused, const bool has_pending_yield, const bool route_call_pending) {
  if (paused) {
    return RouteEntryAction::kWaitPaused;
  }
  if (has_pending_yield) {
    return route_call_pending ? RouteEntryAction::kWaitInFlight : RouteEntryAction::kApplyYield;
  }
  return RouteEntryAction::kContinue;
}

// 当前要去的站点：退避中是避让点，否则是本程目的地。
inline const std::string& RouteTarget(
    const std::string& goal_station, const std::string& backoff_station) {
  return backoff_station.empty() ? goal_station : backoff_station;
}

// 应用一条让路指令之后的退避目标。
// 改道取代还没走完的退避：新路径通往本程目的地，留着避让点的话，车走到路径末站也不算到达，
// 会接着读 path[path.size()]（2026-11-18-r2、2026-11-19-rework2 两次代理崩溃的原因）。
inline std::string BackoffStationAfterYield(
    const std::string& action, const std::vector<std::string>& station_path,
    const std::string& backoff_station) {
  if (action == "reroute") {
    return "";
  }
  if (action == "backoff" && !station_path.empty()) {
    return station_path.back();
  }
  return backoff_station;
}

// 路径上的下一站。越界时抛 std::out_of_range，调用方不要捕获：压测只按进程退出和完成单数
// 判定，报错后继续走会把故障变成静默。
inline const std::string& NextStation(
    const std::vector<std::string>& path, const std::size_t index) {
  return path.at(index);
}

}  // namespace robot_simulation
