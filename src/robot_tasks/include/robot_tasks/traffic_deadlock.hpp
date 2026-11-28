#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "robot_tasks/station_catalog.hpp"
#include "robot_tasks/traffic_reservation.hpp"

namespace robot_tasks {

// ---------------------------------------------------------------------------
// M3 等待图死锁检测与解除
//
// 等待图：节点是车。车 A 正在等待的某个资源被车 B 占用（或暂扣给 B），就有一条 A → B
// 的边；封路标记不是节点。图中有环即死锁。rolling 下一辆车只等一组资源（路段 + 路口），
// 但这两个资源可能被两辆不同的车占着，所以一个节点可能有多条出边，找环用通用的 DFS。
//
// 解除：环上选一辆让路车（优先级最低 → 到目的地剩余路程最长 → 车辆号最大），
// 先试改道（绕开环上他车占用的资源到原目的地），不行就退到最近的空闲避让点；
// 同一组车 30 秒内第三次成环或找不到避让点，则全部暂停等人工处理。
//
// 全部是不依赖 ROS 的纯函数，时间以秒为单位由调用方传入（单调时钟）。
// ---------------------------------------------------------------------------

// whole_route 被拒的申请：不排队，但在有效期内计入等待图（车在原地反复重试）。
struct WholeRouteRetryWait {
  std::string robot_id;
  std::string from_station;
  std::string to_station;
  std::string goal_station;
  int priority = 0;
  std::vector<std::string> blocking_resource_ids;
  double expires_s = 0.0;
};
using WholeRouteRetryWaitMap = std::unordered_map<std::string, WholeRouteRetryWait>;

struct WaitGraphEdge {
  std::string from_robot_id;
  std::string to_robot_id;
  // 构成这条边的资源（from 在等、to 占着或暂扣着），已排序。
  std::vector<std::string> resource_ids;
};

struct WaitCycle {
  // 从字典序最小的车开始，沿等待方向排列：robot_ids[i] 等 robot_ids[i+1]，最后一辆等第一辆。
  std::vector<std::string> robot_ids;
  // 构成环的等待边上的资源，已排序去重。
  std::vector<std::string> resource_ids;
  // 与 robot_ids 一一对应：edges[i] 是 robot_ids[i] → robot_ids[(i+1) % n]。
  std::vector<WaitGraphEdge> edges;
};

// 环上车辆集合的标识（排序后用逗号连接），用于判断"同一组车"。
std::string WaitCycleKey(const WaitCycle& cycle);
// 如 "robot_1 -[route_node:B]-> robot_2 -[route_node:A]-> robot_1"。
std::string DescribeWaitCycle(const WaitCycle& cycle);

// 删掉已过期的 whole_route 重试等待。
void PruneWholeRouteRetryWaits(WholeRouteRetryWaitMap& retry_waits, double now_s);

// 由占用表、等待队列、whole_route 重试等待（未过期的）和暂扣表构建等待图的边。
// 同一对车之间只有一条边，资源合并。结果按 (from, to) 排序。
std::vector<WaitGraphEdge> BuildWaitGraph(
    const RouteLockMap& route_locks, const RouteWaitQueueMap& wait_queues,
    const WholeRouteRetryWaitMap& retry_waits, const RouteHoldMap& holds, double now_s);

// 找出互不相交的环（三色 DFS；找到一个环后把环上的车剔除再继续找）。结果确定、可复现。
std::vector<WaitCycle> FindWaitCycles(const std::vector<WaitGraphEdge>& edges);

// 把环翻译成 /v2/detect_traffic_deadlock 的应答字段（字段名沿用旧接口）。
TrafficDeadlockDecision BuildTrafficDeadlockDecision(const std::vector<WaitCycle>& cycles);

// 一步到位：建图 + 找环 + 翻译。
TrafficDeadlockDecision PlanTrafficDeadlockDetection(
    const RouteLockMap& route_locks, const RouteWaitQueueMap& wait_queues,
    const WholeRouteRetryWaitMap& retry_waits, const RouteHoldMap& holds, double now_s);

// 一辆车当前等待的信息（取自等待队列或 whole_route 重试等待）。
struct TrafficWaitInfo {
  std::string robot_id;
  int priority = 0;
  std::string current_station;
  std::string goal_station;
};
std::optional<TrafficWaitInfo> FindTrafficWaitInfo(
    const std::string& robot_id, const RouteWaitQueueMap& wait_queues,
    const WholeRouteRetryWaitMap& retry_waits, double now_s);

// 在路网上求 from → to 的最短路，绕开 avoid_resource_ids 里的路段锁和路口锁
// （起点自己的路口锁不算）。没有路径返回 nullopt。distance_m 写出路径总长。
std::optional<std::vector<std::string>> FindRoutePathAvoiding(
    const StationCatalog& catalog, const std::string& from, const std::string& to,
    const std::vector<std::string>& avoid_resource_ids, double* distance_m = nullptr);

// 选让路车：优先级最低 → 到目的地剩余最短路程最长（不知道目的地按 0）→ 车辆号最大。
// info 里没有的车按优先级 0、剩余路程 0 处理。
std::string SelectYieldRobot(
    const WaitCycle& cycle, const std::unordered_map<std::string, TrafficWaitInfo>& info,
    const StationCatalog& catalog);
// 同上规则给环上全部车排序，"更该让路"的在前。
std::vector<std::string> RankYieldRobots(
    const WaitCycle& cycle, const std::unordered_map<std::string, TrafficWaitInfo>& info,
    const StationCatalog& catalog);

// 直接或间接在等 robot_id 的车（沿等待边反向可达，不含 robot_id 自己），已排序。
// 这些车在 robot_id 动之前都动不了，让路车开进它们占着的资源只会立刻成新环。
std::vector<std::string> UpstreamWaitingRobots(
    const std::vector<WaitGraphEdge>& edges, const std::string& robot_id);

struct DeadlockYieldPlan {
  std::string robot_id;
  // reroute / backoff / pause
  std::string action;
  std::vector<std::string> cycle_robot_ids;
  std::vector<std::string> avoid_resource_ids;
  std::vector<std::string> station_path;
  double hold_s = 0.0;
  std::string message;
  bool peeling = false;
};

// 为让路车规划解除动作：先试改道（绕开环上他车占用或暂扣的资源和封路，到原目的地），
// 再试退避（去最近的、未被他车占用或暂扣的避让点），都不行返回 pause。
// edges 非空时，让路车还要绕开直接或间接在等它的车占用 / 暂扣的资源（见 UpstreamWaitingRobots）。
DeadlockYieldPlan PlanDeadlockYield(
    const WaitCycle& cycle, const TrafficWaitInfo& yielder, const StationCatalog& catalog,
    const RouteLockMap& route_locks, const RouteHoldMap& holds, double hold_s,
    const std::vector<WaitGraphEdge>* edges = nullptr);

// 按 RankYieldRobots 的顺序逐辆试 PlanDeadlockYield（带 edges），取第一个改道或退避方案；
// info 里没有的车不能让路（不知道它在哪）。
// rolling 的首个方案为退避时，优先选当前全程可用的方案。
// rolling 为 true 且环内全部只能 pause 时，沿等待边逆向分层尝试剥离环外上游等待车。
// 仍无解才返回 pause。
DeadlockYieldPlan PlanDeadlockResolution(
    const WaitCycle& cycle, const std::unordered_map<std::string, TrafficWaitInfo>& info,
    const StationCatalog& catalog, const RouteLockMap& route_locks, const RouteHoldMap& holds,
    const std::vector<WaitGraphEdge>& edges, double hold_s, bool rolling = false,
    const std::unordered_map<std::string, std::string>& home_owners = {});

// 兜底计数：记下"这组车"又成环一次，返回窗口（默认 30 秒）内的成环次数（含本次）。
struct DeadlockHistory {
  std::unordered_map<std::string, std::vector<double>> occurrences_s;
};
int RecordDeadlockOccurrence(
    DeadlockHistory& history, const std::string& cycle_key, double now_s, double window_s = 30.0);

// 正在处理中的环（已检出、尚未解开）。
struct ActiveDeadlock {
  WaitCycle cycle;
  double detected_s = 0.0;
  // 已采取的动作：reroute / backoff / pause；空 = 只检测没解除（解除已关闭）。
  std::string action;
  std::string yield_robot_id;
  std::string peeling_robot_id = {};
  double peeling_started_s = 0.0;
};
using ActiveDeadlockMap = std::unordered_map<std::string, ActiveDeadlock>;

struct ResolvedDeadlock {
  ActiveDeadlock deadlock;
  double resolution_ms = 0.0;
};

struct DeadlockTrackUpdate {
  // 本次需要当作"新检出"处理的环（第一次出现，或改道 / 退避后超时仍在）。
  std::vector<WaitCycle> new_cycles;
  // 已解开的环：上次在、这次不在。耗时 = 检出到这次检测。
  std::vector<ResolvedDeadlock> resolved;
};

// 用本次检测结果更新 active：解开的移出并报告耗时；改道 / 退避后超过 yield_timeout_s
// 仍在的环移出并作为新环重新处理；新出现的环只报告，由调用方决定动作后再放进 active。
DeadlockTrackUpdate TrackDeadlocks(
    ActiveDeadlockMap& active, const std::vector<WaitCycle>& current, double now_s,
    double yield_timeout_s);

}  // namespace robot_tasks
