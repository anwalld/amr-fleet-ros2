#include "robot_tasks/traffic_deadlock.hpp"

#include <gtest/gtest.h>

#include <tuple>

namespace robot_tasks {
namespace {

// 排一个等待者：占着 held，等 wanted（整组）。
void Wait(
    RouteLockMap& locks, RouteWaitQueueMap& queues, std::uint64_t& seq,
    const std::string& robot_id, const std::vector<std::string>& wanted, const int priority = 0,
    const std::string& from = "", const std::string& goal = "") {
  RouteSegmentRequest request;
  request.robot_id = robot_id;
  request.from_station = from;
  request.to_station = "x";
  request.priority = priority;
  request.resource_ids = wanted;
  request.goal_station = goal;
  const auto decision = PlanRouteSegmentRequest(request, locks, queues, &seq);
  ASSERT_TRUE(decision.queued) << decision.message;
}

std::vector<WaitCycle> Detect(
    const RouteLockMap& locks, const RouteWaitQueueMap& queues,
    const WholeRouteRetryWaitMap& retry = {}, const RouteHoldMap& holds = {},
    const double now_s = 0.0) {
  return FindWaitCycles(BuildWaitGraph(locks, queues, retry, holds, now_s));
}

Station MakeStation(const std::string& id, const double x, const double y) {
  Station station;
  station.id = id;
  station.x = x;
  station.y = y;
  return station;
}

StationEdge MakeEdge(const std::string& from, const std::string& to, const double distance) {
  StationEdge edge;
  edge.from = from;
  edge.to = to;
  edge.distance_m = distance;
  edge.bidirectional = true;
  return edge;
}

// 测试路网：
//
//   a ── b ── c ── goal
//   │         │
//   d ─────── e        park_1 挂在 a 上，park_2 挂在 c 上（park_2 更远）
//
// a–b–c 是直路（6 m），a–d–e–c 是绕行（12 m）。
StationCatalog TestCatalog() {
  StationCatalog catalog;
  for (const auto& [id, x, y] : std::vector<std::tuple<std::string, double, double>>{
           {"a", 0, 0}, {"b", 3, 0}, {"c", 6, 0}, {"goal", 9, 0}, {"d", 0, 3},
           {"e", 6, 3}, {"park_1", 0, -3}, {"park_2", 6, -6}}) {
    catalog.stations.push_back(MakeStation(id, x, y));
  }
  catalog.edges = {
      MakeEdge("a", "b", 3),      MakeEdge("b", "c", 3),      MakeEdge("c", "goal", 3),
      MakeEdge("a", "d", 3),      MakeEdge("d", "e", 6),      MakeEdge("e", "c", 3),
      MakeEdge("a", "park_1", 3), MakeEdge("c", "park_2", 6),
  };
  return catalog;
}

}  // namespace

// ---------------------------------------------------------------------------
// 等待图与找环
// ---------------------------------------------------------------------------

TEST(WaitGraphTest, NoCycle_WhenOnlyOneWaits) {
  RouteLockMap locks{{"route_node:a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_2", {"route_node:a"});

  const auto edges = BuildWaitGraph(locks, queues, {}, {}, 0.0);
  ASSERT_EQ(edges.size(), 1U);
  EXPECT_EQ(edges[0].from_robot_id, "robot_2");
  EXPECT_EQ(edges[0].to_robot_id, "robot_1");
  EXPECT_TRUE(FindWaitCycles(edges).empty());
  EXPECT_FALSE(BuildTrafficDeadlockDecision({}).deadlocked);
}

TEST(WaitGraphTest, TwoRobotsHeadOn_OneCycle) {
  RouteLockMap locks{{"route_node:A", "robot_1"}, {"route_node:B", "robot_2"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_1", {"route_edge:A__B", "route_node:B"});
  Wait(locks, queues, seq, "robot_2", {"route_edge:A__B", "route_node:A"});

  const auto cycles = Detect(locks, queues);
  ASSERT_EQ(cycles.size(), 1U);
  EXPECT_EQ(cycles[0].robot_ids, (std::vector<std::string>{"robot_1", "robot_2"}));
  EXPECT_EQ(cycles[0].resource_ids, (std::vector<std::string>{"route_node:A", "route_node:B"}));
  EXPECT_EQ(
      DescribeWaitCycle(cycles[0]),
      "robot_1 -[route_node:B]-> robot_2 -[route_node:A]-> robot_1");
  EXPECT_EQ(WaitCycleKey(cycles[0]), "robot_1,robot_2");
}

TEST(WaitGraphTest, ThreeRobotRing_OneCycle) {
  RouteLockMap locks{
      {"route_node:A", "robot_1"}, {"route_node:B", "robot_2"}, {"route_node:C", "robot_3"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_1", {"route_edge:A__B", "route_node:B"});
  Wait(locks, queues, seq, "robot_2", {"route_edge:B__C", "route_node:C"});
  Wait(locks, queues, seq, "robot_3", {"route_edge:A__C", "route_node:A"});

  const auto cycles = Detect(locks, queues);
  ASSERT_EQ(cycles.size(), 1U);
  EXPECT_EQ(cycles[0].robot_ids, (std::vector<std::string>{"robot_1", "robot_2", "robot_3"}));
  ASSERT_EQ(cycles[0].edges.size(), 3U);
  EXPECT_EQ(cycles[0].edges[2].from_robot_id, "robot_3");
  EXPECT_EQ(cycles[0].edges[2].to_robot_id, "robot_1");
}

TEST(WaitGraphTest, WaiterOutsideCycle_NotInCycle) {
  RouteLockMap locks{{"route_node:A", "robot_1"}, {"route_node:B", "robot_2"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_1", {"route_node:B"});
  Wait(locks, queues, seq, "robot_2", {"route_node:A"});
  Wait(locks, queues, seq, "robot_3", {"route_node:A"});

  const auto cycles = Detect(locks, queues);
  ASSERT_EQ(cycles.size(), 1U);
  EXPECT_EQ(cycles[0].robot_ids, (std::vector<std::string>{"robot_1", "robot_2"}));
  const auto decision = BuildTrafficDeadlockDecision(cycles);
  EXPECT_EQ(decision.mission_ids, (std::vector<std::string>{"robot_1", "robot_2"}));
}

TEST(WaitGraphTest, GroupBlockedByTwoRobots_CycleThroughEither) {
  // robot_1 要的路段被 robot_2 占、路口被 robot_3 占；robot_3 又等 robot_1 的路口。
  RouteLockMap locks{
      {"route_node:A", "robot_1"}, {"route_edge:A__B", "robot_2"}, {"route_node:B", "robot_3"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_1", {"route_edge:A__B", "route_node:B"});
  Wait(locks, queues, seq, "robot_3", {"route_node:A"});

  const auto edges = BuildWaitGraph(locks, queues, {}, {}, 0.0);
  EXPECT_EQ(edges.size(), 3U);  // robot_1 有两条出边
  const auto cycles = FindWaitCycles(edges);
  ASSERT_EQ(cycles.size(), 1U);
  EXPECT_EQ(cycles[0].robot_ids, (std::vector<std::string>{"robot_1", "robot_3"}));
}

TEST(WaitGraphTest, TwoDisjointCycles_BothFound) {
  RouteLockMap locks{
      {"route_node:A", "robot_1"}, {"route_node:B", "robot_2"},
      {"route_node:C", "robot_3"}, {"route_node:D", "robot_4"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_1", {"route_node:B"});
  Wait(locks, queues, seq, "robot_2", {"route_node:A"});
  Wait(locks, queues, seq, "robot_3", {"route_node:D"});
  Wait(locks, queues, seq, "robot_4", {"route_node:C"});

  const auto cycles = Detect(locks, queues);
  ASSERT_EQ(cycles.size(), 2U);
  EXPECT_EQ(WaitCycleKey(cycles[0]), "robot_1,robot_2");
  EXPECT_EQ(WaitCycleKey(cycles[1]), "robot_3,robot_4");
}

TEST(WaitGraphTest, TrafficBlockIsNotANode) {
  RouteLockMap locks{{"route_edge:A__B", "traffic_block:manual"}, {"route_node:A", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_2", {"route_node:A"});
  const auto edges = BuildWaitGraph(locks, queues, {}, {}, 0.0);
  ASSERT_EQ(edges.size(), 1U);
  EXPECT_EQ(edges[0].to_robot_id, "robot_1");
}

TEST(WaitGraphTest, WholeRouteRetryWait_CountsUntilExpired) {
  RouteLockMap locks{{"route_node:A", "robot_1"}, {"route_node:B", "robot_2"}};
  WholeRouteRetryWaitMap retry{
      {"robot_1", WholeRouteRetryWait{"robot_1", "A", "C", "C", 0, {"route_node:B"}, 6.0}},
      {"robot_2", WholeRouteRetryWait{"robot_2", "B", "D", "D", 0, {"route_node:A"}, 6.0}},
  };

  EXPECT_EQ(Detect(locks, {}, retry, {}, 5.0).size(), 1U);
  EXPECT_TRUE(Detect(locks, {}, retry, {}, 6.0).empty());

  PruneWholeRouteRetryWaits(retry, 6.0);
  EXPECT_TRUE(retry.empty());
}

TEST(WaitGraphTest, HoldCreatesEdgeToHolder) {
  RouteLockMap locks{{"route_node:A", "robot_2"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_1", {"route_node:A", "route_node:B"});
  locks.erase("route_node:A");
  locks["route_node:C"] = "robot_9";
  Wait(locks, queues, seq, "robot_2", {"route_node:C"});
  RouteHoldMap holds{{"route_node:B", RouteHold{"robot_9", 5}}};

  const auto edges = BuildWaitGraph(locks, queues, {}, holds, 0.0);
  // robot_1 → robot_9（暂扣 B）；robot_2 → robot_9（占 C）。
  ASSERT_EQ(edges.size(), 2U);
  EXPECT_EQ(edges[0].from_robot_id, "robot_1");
  EXPECT_EQ(edges[0].to_robot_id, "robot_9");
  EXPECT_EQ(edges[0].resource_ids, (std::vector<std::string>{"route_node:B"}));
}

TEST(DeadlockDetectionTest, HeadOn_FillsLegacyResponseFields) {
  RouteLockMap locks{{"route_node:storage_a", "robot_alpha"}, {"route_node:packing", "robot_beta"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  Wait(locks, queues, seq, "robot_alpha", {"route_edge:packing__storage_a", "route_node:packing"});
  Wait(locks, queues, seq, "robot_beta", {"route_edge:packing__storage_a", "route_node:storage_a"});

  const auto decision = PlanTrafficDeadlockDetection(locks, queues, {}, {}, 0.0);
  EXPECT_TRUE(decision.deadlocked);
  EXPECT_EQ(decision.mission_ids, (std::vector<std::string>{"robot_alpha", "robot_beta"}));
  EXPECT_EQ(
      decision.conflict_resource_ids,
      (std::vector<std::string>{"route_node:packing", "route_node:storage_a"}));
  EXPECT_EQ(decision.conflict_station_ids, (std::vector<std::string>{"packing", "storage_a"}));
  ASSERT_EQ(decision.descriptions.size(), 1U);
  EXPECT_EQ(decision.message, "traffic deadlock detected: 1 wait cycle(s)");
}

TEST(DeadlockDetectionTest, SharedStationWithoutWaiting_NotDeadlocked) {
  // M2 时代的"冲突候选"：两车的路段锁共同触及 storage_a，但没人在等。
  RouteLockMap locks{
      {"route_edge:receiving__storage_a", "robot_alpha"},
      {"route_edge:packing__storage_a", "robot_beta"}};

  const auto decision = PlanTrafficDeadlockDetection(locks, {}, {}, {}, 0.0);
  EXPECT_FALSE(decision.deadlocked);
  EXPECT_TRUE(decision.mission_ids.empty());
  EXPECT_EQ(decision.message, "no traffic deadlock detected");
}

// ---------------------------------------------------------------------------
// 选让路车、改道、退避
// ---------------------------------------------------------------------------

TEST(YieldSelectionTest, LowestPriorityYields) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  const std::unordered_map<std::string, TrafficWaitInfo> info{
      {"robot_1", {"robot_1", 20, "a", ""}}, {"robot_2", {"robot_2", 10, "c", ""}}};
  EXPECT_EQ(SelectYieldRobot(cycle, info, TestCatalog()), "robot_2");
}

TEST(YieldSelectionTest, SamePriority_LongerRemainingRouteYields) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  // robot_1 在 c 去 goal（3 m）；robot_2 在 a 去 goal（9 m）。
  const std::unordered_map<std::string, TrafficWaitInfo> info{
      {"robot_1", {"robot_1", 0, "c", "goal"}}, {"robot_2", {"robot_2", 0, "a", "goal"}}};
  EXPECT_EQ(SelectYieldRobot(cycle, info, TestCatalog()), "robot_2");
}

TEST(YieldSelectionTest, FullTie_LargestRobotIdYields) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_3", "robot_2"};
  EXPECT_EQ(SelectYieldRobot(cycle, {}, TestCatalog()), "robot_3");
}

TEST(YieldPlanTest, RerouteAroundCycleResources) {
  // robot_2 在 a 去 goal，被 robot_1（占着 b）挡住；绕 d–e–c 可达。
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  const RouteLockMap locks{{"route_node:b", "robot_1"}, {"route_node:a", "robot_2"}};
  const auto plan = PlanDeadlockYield(
      cycle, TrafficWaitInfo{"robot_2", 0, "a", "goal"}, TestCatalog(), locks, {}, 5.0);

  EXPECT_EQ(plan.action, "reroute");
  EXPECT_EQ(plan.station_path, (std::vector<std::string>{"a", "d", "e", "c", "goal"}));
  EXPECT_EQ(plan.avoid_resource_ids, (std::vector<std::string>{"route_node:b"}));
}

TEST(YieldPlanTest, NoReroute_BackoffToNearestFreeYieldPoint) {
  // 目的地 b 就被环上的 robot_1 占着：改不了道，退到最近的空闲避让点 park_1。
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  const RouteLockMap locks{{"route_node:b", "robot_1"}, {"route_node:a", "robot_2"}};
  const auto plan = PlanDeadlockYield(
      cycle, TrafficWaitInfo{"robot_2", 0, "a", "b"}, TestCatalog(), locks, {}, 5.0);

  EXPECT_EQ(plan.action, "backoff");
  EXPECT_EQ(plan.station_path, (std::vector<std::string>{"a", "park_1"}));
  EXPECT_DOUBLE_EQ(plan.hold_s, 5.0);
}

TEST(YieldPlanTest, UnknownGoal_Backoff_SkipsOccupiedYieldPoint) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  // park_1 被别的车占着：退到 park_2（绕 d–e–c，避开环上 robot_1 占着的 b）。
  const RouteLockMap locks{
      {"route_node:b", "robot_1"}, {"route_node:a", "robot_2"}, {"route_node:park_1", "robot_7"}};
  const auto plan = PlanDeadlockYield(
      cycle, TrafficWaitInfo{"robot_2", 0, "a", ""}, TestCatalog(), locks, {}, 5.0);

  EXPECT_EQ(plan.action, "backoff");
  EXPECT_EQ(plan.station_path, (std::vector<std::string>{"a", "d", "e", "c", "park_2"}));
}

TEST(YieldPlanTest, NoFreeYieldPoint_Pause) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  const RouteLockMap locks{
      {"route_node:b", "robot_1"}, {"route_node:a", "robot_2"},
      {"route_node:park_1", "robot_7"}, {"route_node:park_2", "robot_8"}};
  const auto plan = PlanDeadlockYield(
      cycle, TrafficWaitInfo{"robot_2", 0, "a", ""}, TestCatalog(), locks, {}, 5.0);
  EXPECT_EQ(plan.action, "pause");
}

TEST(YieldPlanTest, ExplicitYieldPointFlagOverridesPrefix) {
  auto catalog = TestCatalog();
  for (auto& station : catalog.stations) {
    if (station.id == "park_1") {
      station.yield_point = false;
    }
    if (station.id == "d") {
      station.yield_point = true;
    }
  }
  EXPECT_FALSE(IsYieldPoint(*FindStation(catalog, "park_1")));
  EXPECT_TRUE(IsYieldPoint(*FindStation(catalog, "park_2")));
  EXPECT_TRUE(IsYieldPoint(*FindStation(catalog, "d")));
}

TEST(YieldPlanTest, PathAvoidingBlockedEdge) {
  double distance = 0.0;
  const auto path =
      FindRoutePathAvoiding(TestCatalog(), "a", "c", {"route_edge:a__b"}, &distance);
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(*path, (std::vector<std::string>{"a", "d", "e", "c"}));
  EXPECT_DOUBLE_EQ(distance, 12.0);
  EXPECT_FALSE(FindRoutePathAvoiding(TestCatalog(), "a", "goal", {"route_node:c"}).has_value());
}

// ---------------------------------------------------------------------------
// 兜底与耗时
// ---------------------------------------------------------------------------

TEST(DeadlockFallbackTest, ThirdCycleWithin30s_Counts3) {
  DeadlockHistory history;
  EXPECT_EQ(RecordDeadlockOccurrence(history, "robot_1,robot_2", 0.0), 1);
  EXPECT_EQ(RecordDeadlockOccurrence(history, "robot_1,robot_2", 10.0), 2);
  EXPECT_EQ(RecordDeadlockOccurrence(history, "robot_1,robot_3", 12.0), 1);
  EXPECT_EQ(RecordDeadlockOccurrence(history, "robot_1,robot_2", 25.0), 3);
}

TEST(DeadlockFallbackTest, OlderThan30s_NotAccumulated) {
  DeadlockHistory history;
  RecordDeadlockOccurrence(history, "robot_1,robot_2", 0.0);
  RecordDeadlockOccurrence(history, "robot_1,robot_2", 10.0);
  EXPECT_EQ(RecordDeadlockOccurrence(history, "robot_1,robot_2", 41.0), 1);
}

TEST(DeadlockTrackTest, ResolvedCycleReportsElapsedMs) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  ActiveDeadlockMap active;

  auto update = TrackDeadlocks(active, {cycle}, 100.0, 10.0);
  ASSERT_EQ(update.new_cycles.size(), 1U);
  active[WaitCycleKey(cycle)] = ActiveDeadlock{cycle, 100.0, "reroute", "robot_2"};

  update = TrackDeadlocks(active, {cycle}, 100.5, 10.0);  // 仍在，未超时
  EXPECT_TRUE(update.new_cycles.empty());
  EXPECT_TRUE(update.resolved.empty());

  update = TrackDeadlocks(active, {}, 101.2, 10.0);
  ASSERT_EQ(update.resolved.size(), 1U);
  EXPECT_NEAR(update.resolved[0].resolution_ms, 1200.0, 1e-6);
  EXPECT_EQ(update.resolved[0].deadlock.action, "reroute");
  EXPECT_TRUE(active.empty());
}

TEST(DeadlockTrackTest, YieldTimeout_TreatedAsNewCycle) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  ActiveDeadlockMap active{{WaitCycleKey(cycle), ActiveDeadlock{cycle, 0.0, "backoff", "robot_2"}}};

  const auto update = TrackDeadlocks(active, {cycle}, 11.0, 10.0);
  ASSERT_EQ(update.new_cycles.size(), 1U);
  EXPECT_TRUE(active.empty());
}

TEST(DeadlockTrackTest, DetectOnlyOrPaused_NeverTimesOut) {
  WaitCycle cycle;
  cycle.robot_ids = {"robot_1", "robot_2"};
  ActiveDeadlockMap active{{WaitCycleKey(cycle), ActiveDeadlock{cycle, 0.0, "", ""}}};
  EXPECT_TRUE(TrackDeadlocks(active, {cycle}, 100.0, 10.0).new_cycles.empty());
  active[WaitCycleKey(cycle)].action = "pause";
  EXPECT_TRUE(TrackDeadlocks(active, {cycle}, 200.0, 10.0).new_cycles.empty());
}

}  // namespace robot_tasks
