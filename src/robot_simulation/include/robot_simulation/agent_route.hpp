#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace robot_simulation {

struct DirectedEdge {
  std::string to;
  double distance_m = 0.0;
};

using RouteGraph = std::map<std::string, std::vector<DirectedEdge>>;
// 无序站点对。
using EdgeKey = std::pair<std::string, std::string>;

inline EdgeKey MakeEdgeKey(const std::string& a, const std::string& b) {
  return a <= b ? EdgeKey{a, b} : EdgeKey{b, a};
}

// 避开封路和临时绕行的最短路；绕行的路口是终点时不避开。无路时为空。
inline std::vector<std::string> ShortestPath(
    const RouteGraph& graph, const std::string& from, const std::string& to,
    const std::set<EdgeKey>& blocked_edges, const std::set<EdgeKey>& avoid_edges,
    const std::set<std::string>& avoid_nodes) {
  if (from == to) {
    return {from};
  }
  std::map<std::string, double> dist;
  std::map<std::string, std::string> prev;
  using Item = std::pair<double, std::string>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
  dist[from] = 0.0;
  open.push({0.0, from});
  while (!open.empty()) {
    const auto [d, u] = open.top();
    open.pop();
    if (d > dist[u]) {
      continue;
    }
    if (u == to) {
      break;
    }
    const auto it = graph.find(u);
    if (it == graph.end()) {
      continue;
    }
    for (const auto& e : it->second) {
      const auto key = MakeEdgeKey(u, e.to);
      if (blocked_edges.count(key) != 0U || avoid_edges.count(key) != 0U ||
          (e.to != to && avoid_nodes.count(e.to) != 0U)) {
        continue;
      }
      const double nd = d + e.distance_m;
      const auto old = dist.find(e.to);
      if (old == dist.end() || nd < old->second) {
        dist[e.to] = nd;
        prev[e.to] = u;
        open.push({nd, e.to});
      }
    }
  }
  if (dist.find(to) == dist.end()) {
    return {};
  }
  std::vector<std::string> path{to};
  while (path.back() != from) {
    path.push_back(prev.at(path.back()));
  }
  std::reverse(path.begin(), path.end());
  return path;
}

struct RoutePlan {
  std::vector<std::string> path;
  bool detours_dropped = false;
};

// 去当前目标的路径。临时绕行让目标不可达时丢掉绕行再找一次，否则车会一直停在原地重试；
// 封路始终避开。
inline RoutePlan PlanRoute(
    const RouteGraph& graph, const std::string& from, const std::string& to,
    const std::set<EdgeKey>& blocked_edges, std::set<EdgeKey>& avoid_edges,
    std::set<std::string>& avoid_nodes) {
  RoutePlan plan;
  plan.path = ShortestPath(graph, from, to, blocked_edges, avoid_edges, avoid_nodes);
  if (plan.path.empty() && (!avoid_edges.empty() || !avoid_nodes.empty())) {
    avoid_edges.clear();
    avoid_nodes.clear();
    plan.detours_dropped = true;
    plan.path = ShortestPath(graph, from, to, blocked_edges, avoid_edges, avoid_nodes);
  }
  return plan;
}

}  // namespace robot_simulation
