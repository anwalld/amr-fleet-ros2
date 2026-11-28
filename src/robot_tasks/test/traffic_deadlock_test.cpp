#include "robot_tasks/traffic_deadlock.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <tuple>
#include <unordered_set>

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


// ---------------------------------------------------------------------------
// 让路车不能开进"在等它的车"占着的路口（第一夜 4 台 rolling 的实况）
//
//   park_a ── n ── m ── s ── park_b
//             │         │
//             g2(远)    g3
//
// robot_2 在 m 要去 g2（经 n），robot_3 在 n 要去 g3（经 m），二者对向成环；
// robot_4 在 s 也在等 m（环外，但在等 robot_2）。robot_2 剩余路程最长，按规则先选它，
// 可它两边的 n、s 都被在等它的车占着，开过去只会立刻成新环——应改由 robot_3 退到 park_a。
// ---------------------------------------------------------------------------
namespace {

StationCatalog SurroundedCatalog() {
  StationCatalog catalog;
  for (const auto& [id, x, y] : std::vector<std::tuple<std::string, double, double>>{
           {"m", 0, 0}, {"n", 0, 3}, {"s", 0, -3}, {"park_a", -3, 3}, {"park_b", 3, -3},
           {"g2", 0, 23}, {"g3", 0, -12}}) {
    catalog.stations.push_back(MakeStation(id, x, y));
  }
  catalog.edges = {
      MakeEdge("park_a", "n", 3), MakeEdge("n", "m", 3),  MakeEdge("m", "s", 3),
      MakeEdge("s", "park_b", 3), MakeEdge("n", "g2", 20), MakeEdge("s", "g3", 9),
  };
  return catalog;
}

struct SurroundedCase {
  RouteLockMap locks{{"route_node:m", "robot_2"}, {"route_node:n", "robot_3"},
                     {"route_node:s", "robot_4"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
};

void SetUpSurrounded(SurroundedCase& c) {
  Wait(c.locks, c.queues, c.seq, "robot_2", {"route_node:n"}, 0, "m", "g2");
  Wait(c.locks, c.queues, c.seq, "robot_3", {"route_node:m"}, 0, "n", "g3");
  Wait(c.locks, c.queues, c.seq, "robot_4", {"route_node:m"}, 0, "s", "g3");
}

std::unordered_map<std::string, TrafficWaitInfo> InfoFor(
    const WaitCycle& cycle, const SurroundedCase& c) {
  std::unordered_map<std::string, TrafficWaitInfo> info;
  for (const auto& robot_id : cycle.robot_ids) {
    if (const auto wait = FindTrafficWaitInfo(robot_id, c.queues, {}, 0.0)) {
      info[robot_id] = *wait;
    }
  }
  return info;
}

}  // namespace

TEST(YieldUpstreamTest, UpstreamWaitingRobots_FollowsWaitEdgesBackwards) {
  SurroundedCase c;
  SetUpSurrounded(c);
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, {}, 0.0);
  EXPECT_EQ(UpstreamWaitingRobots(edges, "robot_2"),
            (std::vector<std::string>{"robot_3", "robot_4"}));
  EXPECT_EQ(UpstreamWaitingRobots(edges, "robot_3"),
            (std::vector<std::string>{"robot_2", "robot_4"}));
  EXPECT_TRUE(UpstreamWaitingRobots(edges, "robot_4").empty());
}

TEST(YieldUpstreamTest, SurroundedYielder_WithoutEdgesWouldDriveIntoWaiter) {
  SurroundedCase c;
  SetUpSurrounded(c);
  const auto catalog = SurroundedCatalog();
  const auto cycles = Detect(c.locks, c.queues);
  ASSERT_EQ(cycles.size(), 1U);
  ASSERT_EQ(cycles[0].robot_ids, (std::vector<std::string>{"robot_2", "robot_3"}));
  const auto info = InfoFor(cycles[0], c);
  ASSERT_EQ(SelectYieldRobot(cycles[0], info, catalog), "robot_2");

  // 旧做法（不看在等它的车）：robot_2 退到 park_b，要经过 robot_4 占着的 s。
  const auto old_plan =
      PlanDeadlockYield(cycles[0], info.at("robot_2"), catalog, c.locks, {}, 5.0);
  EXPECT_EQ(old_plan.action, "backoff");
  EXPECT_EQ(old_plan.station_path, (std::vector<std::string>{"m", "s", "park_b"}));

  // 带等待边：robot_2 无路可走。
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, {}, 0.0);
  const auto plan =
      PlanDeadlockYield(cycles[0], info.at("robot_2"), catalog, c.locks, {}, 5.0, &edges);
  EXPECT_EQ(plan.action, "pause");
}

TEST(YieldUpstreamTest, Resolution_FallsBackToNextRobotThatCanMove) {
  SurroundedCase c;
  SetUpSurrounded(c);
  const auto catalog = SurroundedCatalog();
  const auto cycles = Detect(c.locks, c.queues);
  ASSERT_EQ(cycles.size(), 1U);
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, {}, 0.0);
  const auto plan =
      PlanDeadlockResolution(cycles[0], InfoFor(cycles[0], c), catalog, c.locks, {}, edges, 5.0);
  EXPECT_EQ(plan.robot_id, "robot_3");
  EXPECT_EQ(plan.action, "backoff");
  EXPECT_EQ(plan.station_path, (std::vector<std::string>{"n", "park_a"}));
  EXPECT_DOUBLE_EQ(plan.hold_s, 5.0);
}

TEST(YieldUpstreamTest, Resolution_AllStuck_PauseNamesTopRankedRobot) {
  SurroundedCase c;
  c.locks["route_node:park_a"] = "robot_9";  // 唯一能退的地方也被占了
  SetUpSurrounded(c);
  const auto catalog = SurroundedCatalog();
  const auto cycles = Detect(c.locks, c.queues);
  ASSERT_EQ(cycles.size(), 1U);
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, {}, 0.0);
  const auto plan =
      PlanDeadlockResolution(cycles[0], InfoFor(cycles[0], c), catalog, c.locks, {}, edges, 5.0);
  EXPECT_EQ(plan.action, "pause");
  EXPECT_EQ(plan.robot_id, "robot_2");
}

TEST(YieldUpstreamTest, RollingPrefersClearBackoffOverPathThroughOutsideVehicle) {
  SurroundedCase c;
  Wait(c.locks, c.queues, c.seq, "robot_2", {"route_node:n"}, 0, "m", "g2");
  Wait(c.locks, c.queues, c.seq, "robot_3", {"route_node:m"}, 0, "n", "g3");
  const auto catalog = SurroundedCatalog();
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, {}, 0.0);
  const auto cycles = FindWaitCycles(edges);
  ASSERT_EQ(cycles.size(), 1U);
  const auto info = InfoFor(cycles[0], c);

  const auto whole_route =
      PlanDeadlockResolution(cycles[0], info, catalog, c.locks, {}, edges, 5.0);
  EXPECT_EQ(whole_route.robot_id, "robot_2");
  EXPECT_EQ(whole_route.station_path, (std::vector<std::string>{"m", "s", "park_b"}));

  const auto rolling =
      PlanDeadlockResolution(cycles[0], info, catalog, c.locks, {}, edges, 5.0, true);
  EXPECT_EQ(rolling.robot_id, "robot_3");
  EXPECT_EQ(rolling.action, "backoff");
  EXPECT_EQ(rolling.station_path, (std::vector<std::string>{"n", "park_a"}));
  EXPECT_EQ(c.locks.at("route_node:s"), "robot_4");
}

TEST(YieldUpstreamTest, RollingChecksHoldsAndKeepsOriginalBackoffWhenNoClearPathExists) {
  SurroundedCase c;
  c.locks.erase("route_node:s");
  Wait(c.locks, c.queues, c.seq, "robot_2", {"route_node:n"}, 0, "m", "g2");
  Wait(c.locks, c.queues, c.seq, "robot_3", {"route_node:m"}, 0, "n", "g3");
  const auto catalog = SurroundedCatalog();
  const RouteHoldMap holds{{"route_node:s", RouteHold{"robot_4", 10}}};
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, holds, 0.0);
  const auto cycles = FindWaitCycles(edges);
  ASSERT_EQ(cycles.size(), 1U);
  const auto info = InfoFor(cycles[0], c);

  const auto clear =
      PlanDeadlockResolution(cycles[0], info, catalog, c.locks, holds, edges, 5.0, true);
  EXPECT_EQ(clear.robot_id, "robot_3");
  EXPECT_EQ(clear.station_path, (std::vector<std::string>{"n", "park_a"}));

  c.locks["route_node:park_a"] = "robot_7";
  const auto fallback =
      PlanDeadlockResolution(cycles[0], info, catalog, c.locks, holds, edges, 5.0, true);
  EXPECT_EQ(fallback.robot_id, "robot_2");
  EXPECT_EQ(fallback.action, "backoff");
  EXPECT_EQ(fallback.station_path, (std::vector<std::string>{"m", "s", "park_b"}));
}

TEST(YieldUpstreamTest, RollingPrefersPublicRefugeOverAnEmptyForeignHome) {
  SurroundedCase c;
  c.locks.erase("route_node:s");
  Wait(c.locks, c.queues, c.seq, "robot_2", {"route_node:n"}, 0, "m", "g2");
  Wait(c.locks, c.queues, c.seq, "robot_3", {"route_node:m"}, 0, "n", "g3");
  const auto catalog = SurroundedCatalog();
  const auto edges = BuildWaitGraph(c.locks, c.queues, {}, {}, 0.0);
  const auto cycles = FindWaitCycles(edges);
  ASSERT_EQ(cycles.size(), 1U);
  const auto info = InfoFor(cycles.front(), c);
  std::unordered_map<std::string, std::string> homes{{"park_b", "robot_4"}};

  const auto rolling = PlanDeadlockResolution(
      cycles.front(), info, catalog, c.locks, {}, edges, 5.0, true, homes);
  EXPECT_EQ(rolling.robot_id, "robot_3");
  EXPECT_EQ(rolling.station_path, (std::vector<std::string>{"n", "park_a"}));

  const auto whole_route = PlanDeadlockResolution(
      cycles.front(), info, catalog, c.locks, {}, edges, 5.0, false, homes);
  EXPECT_EQ(whole_route.robot_id, "robot_2");
  EXPECT_EQ(whole_route.station_path, (std::vector<std::string>{"m", "s", "park_b"}));

  homes["park_a"] = "robot_5";
  const auto fallback = PlanDeadlockResolution(
      cycles.front(), info, catalog, c.locks, {}, edges, 5.0, true, homes);
  EXPECT_EQ(fallback.robot_id, whole_route.robot_id);
  EXPECT_EQ(fallback.station_path, whole_route.station_path);

  homes["park_b"].clear();
  const auto shared_home = PlanDeadlockResolution(
      cycles.front(), info, catalog, c.locks, {}, edges, 5.0, true, homes);
  EXPECT_EQ(shared_home.robot_id, whole_route.robot_id);
  EXPECT_EQ(shared_home.station_path, whole_route.station_path);
}

namespace {

StationCatalog Workshop2WayCatalog() {
  StationCatalog catalog;
  for (const auto& [id, x, y, yaw] :
       std::vector<std::tuple<std::string, double, double, double>>{
           {"jn_sw", 0.0, 0.0, 0.0},     {"receiving", 6.0, 0.0, 0.0},
           {"jn_s", 12.0, 0.0, 0.0},    {"jn_se", 18.0, 0.0, 1.57},
           {"assembly", 18.0, 12.0, 3.14}, {"jn_n", 12.0, 12.0, 3.14},
           {"packing", 6.0, 12.0, 3.14}, {"jn_nw", 0.0, 12.0, -1.57},
           {"storage", 0.0, 6.0, -1.57}, {"machining", 12.0, 6.0, 1.57},
           {"park_1", 3.0, -3.0, 0.0},  {"park_2", 15.0, -3.0, 0.0},
           {"park_3", 21.0, 6.0, 1.57}, {"park_4", 15.0, 15.0, 3.14},
           {"park_5", 3.0, 15.0, 3.14}, {"park_6", -3.0, 9.0, -1.57}}) {
    auto station = MakeStation(id, x, y);
    station.yaw = yaw;
    catalog.stations.push_back(station);
  }
  catalog.edges = {
      {"jn_sw", "receiving", 6.00, true}, {"receiving", "jn_s", 6.00, false},
      {"jn_s", "jn_se", 6.00, false},     {"jn_se", "assembly", 12.00, false},
      {"assembly", "jn_n", 6.00, false},  {"jn_n", "packing", 6.00, true},
      {"packing", "jn_nw", 6.00, false}, {"jn_nw", "storage", 6.00, false},
      {"storage", "jn_sw", 6.00, false},  {"jn_s", "machining", 6.00, true},
      {"machining", "jn_n", 6.00, true},  {"jn_sw", "park_1", 4.24, false},
      {"park_1", "receiving", 4.24, false}, {"jn_s", "park_2", 4.24, false},
      {"park_2", "jn_se", 4.24, false},   {"jn_se", "park_3", 6.71, false},
      {"park_3", "assembly", 6.71, false}, {"assembly", "park_4", 4.24, false},
      {"park_4", "jn_n", 4.24, false},    {"packing", "park_5", 4.24, false},
      {"park_5", "jn_nw", 4.24, false},  {"jn_nw", "park_6", 4.24, false},
      {"park_6", "storage", 4.24, false}, {"jn_se", "machining", 8.49, false},
  };
  return catalog;
}

using StationOwners = std::unordered_map<std::string, std::string>;
using StationWaits = std::vector<std::pair<std::string, std::string>>;

struct WorkshopWaitGraph {
  RouteLockMap locks;
  std::vector<WaitGraphEdge> edges;
  std::vector<WaitCycle> cycles;
  std::unordered_map<std::string, TrafficWaitInfo> info;
};

WorkshopWaitGraph BuildWorkshopWaitGraph(const StationOwners& owners, const StationWaits& waits) {
  WorkshopWaitGraph graph;
  for (const auto& [station, robot] : owners) {
    graph.locks.emplace(RouteNodeLockId(station), robot);
  }
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  for (const auto& [from, to] : waits) {
    const auto& robot = owners.at(from);
    Wait(graph.locks, queues, seq, robot, {RouteNodeLockId(to)}, 0, from, to);
    if (const auto wait = FindTrafficWaitInfo(robot, queues, {}, 0.0)) {
      graph.info.emplace(robot, *wait);
    }
  }
  graph.edges = BuildWaitGraph(graph.locks, queues, {}, {}, 0.0);
  graph.cycles = FindWaitCycles(graph.edges);
  return graph;
}

const StationOwners kV4FormalOwners{
    {"jn_s", "robot_1"}, {"machining", "robot_2"}, {"jn_se", "robot_3"},
    {"park_4", "robot_4"}, {"park_2", "robot_5"}, {"jn_n", "robot_6"}};
const StationWaits kV4FormalWaits{
    {"jn_s", "machining"}, {"machining", "jn_s"}, {"jn_n", "machining"},
    {"jn_se", "machining"}, {"park_2", "jn_se"}, {"park_4", "jn_n"}};
const std::vector<std::string> kWorkshopCycleRobots{"robot_1", "robot_2"};

void ExpectClearWorkshopPath(
    const DeadlockYieldPlan& plan, const RouteLockMap& locks,
    const std::unordered_set<std::string>* component = nullptr) {
  ASSERT_GE(plan.station_path.size(), 2U);
  EXPECT_EQ(locks.at(RouteNodeLockId(plan.station_path.front())), plan.robot_id);
  for (std::size_t i = 1; i < plan.station_path.size(); ++i) {
    const auto owner = locks.find(RouteNodeLockId(plan.station_path[i]));
    if (owner != locks.end() && (component == nullptr || component->count(owner->second) != 0U)) {
      EXPECT_EQ(owner->second, plan.robot_id) << plan.station_path[i];
    }
  }
}

}  // namespace

TEST(YieldUpstreamTest, Peeling_CycleSolvableInCycle_DoesNotPeel) {
  const auto catalog = Workshop2WayCatalog();
  const auto graph = BuildWorkshopWaitGraph(
      {{"jn_s", "robot_1"}, {"machining", "robot_2"}, {"jn_se", "robot_3"}},
      {{"jn_s", "machining"}, {"machining", "jn_s"}, {"jn_se", "machining"}});
  ASSERT_EQ(graph.cycles.size(), 1U);
  const auto plan = PlanDeadlockResolution(
      graph.cycles.front(), graph.info, catalog, graph.locks, {}, graph.edges, 5.0, true);
  EXPECT_NE(plan.action, "pause");
  EXPECT_TRUE(plan.robot_id == "robot_1" || plan.robot_id == "robot_2");
  EXPECT_FALSE(plan.peeling);
}

TEST(YieldUpstreamTest, Peeling_V4FormalZeroEscapeScene_PeelsUpstreamWaiter) {
  // v4-formal, t=1606.65s
  const auto catalog = Workshop2WayCatalog();
  const auto graph = BuildWorkshopWaitGraph(kV4FormalOwners, kV4FormalWaits);
  ASSERT_EQ(graph.cycles.size(), 1U);
  const auto& cycle = graph.cycles.front();
  ASSERT_EQ(cycle.robot_ids, kWorkshopCycleRobots);
  const auto plan = PlanDeadlockResolution(
      cycle, graph.info, catalog, graph.locks, {}, graph.edges, 5.0, true);
  EXPECT_TRUE(plan.peeling);
  EXPECT_NE(plan.action, "pause");
  EXPECT_EQ(plan.cycle_robot_ids, kWorkshopCycleRobots);
  const auto upstream = UpstreamWaitingRobots(graph.edges, "robot_1");
  EXPECT_NE(std::find(upstream.begin(), upstream.end(), plan.robot_id), upstream.end());
  EXPECT_NE(plan.robot_id, "robot_2");
  ExpectClearWorkshopPath(plan, graph.locks);
}

TEST(YieldUpstreamTest, Peeling_RollingDiagnosticZeroEscapeScene_PeelsUpstreamWaiter) {
  // rolling-diagnostic, t=1680.52s
  const auto catalog = Workshop2WayCatalog();
  const auto graph = BuildWorkshopWaitGraph(
      {{"jn_n", "robot_1"}, {"machining", "robot_2"}, {"packing", "robot_3"},
       {"jn_s", "robot_5"}},
      {{"jn_n", "machining"}, {"machining", "jn_n"}, {"packing", "jn_n"}, {"jn_s", "machining"}});
  ASSERT_EQ(graph.cycles.size(), 1U);
  const auto plan = PlanDeadlockResolution(
      graph.cycles.front(), graph.info, catalog, graph.locks, {}, graph.edges, 5.0, true);
  EXPECT_TRUE(plan.peeling);
  EXPECT_NE(plan.action, "pause");
  EXPECT_EQ(plan.cycle_robot_ids, kWorkshopCycleRobots);
  EXPECT_TRUE(plan.robot_id == "robot_3" || plan.robot_id == "robot_5");
  ExpectClearWorkshopPath(plan, graph.locks);
  ASSERT_FALSE(plan.station_path.empty());
  const auto* refuge = FindStation(catalog, plan.station_path.back());
  ASSERT_NE(refuge, nullptr);
  EXPECT_TRUE(IsYieldPoint(*refuge));
}

TEST(YieldUpstreamTest, Peeling_NoEscapeAnywhere_FallsBackToPause) {
  const auto catalog = Workshop2WayCatalog();
  auto owners = kV4FormalOwners;
  for (const auto& station : catalog.stations) {
    if (IsYieldPoint(station)) {
      owners.emplace(station.id, "parked_" + station.id);
    }
  }
  const auto graph = BuildWorkshopWaitGraph(owners, kV4FormalWaits);
  ASSERT_EQ(graph.cycles.size(), 1U);
  const auto plan = PlanDeadlockResolution(
      graph.cycles.front(), graph.info, catalog, graph.locks, {}, graph.edges, 5.0, true);
  EXPECT_EQ(plan.action, "pause");
  EXPECT_FALSE(plan.peeling);
}

TEST(DeadlockTrackTest, PeelingTimeoutStartsWhenPeelingBegins) {
  WaitCycle cycle;
  cycle.robot_ids = kWorkshopCycleRobots;
  const auto key = WaitCycleKey(cycle);
  ActiveDeadlockMap active{
      {key, ActiveDeadlock{cycle, 100.0, "backoff", "robot_3", "robot_3", 105.0}}};

  EXPECT_TRUE(TrackDeadlocks(active, {cycle}, 111.0, 10.0).new_cycles.empty());
  ASSERT_EQ(active.count(key), 1U);
  const auto update = TrackDeadlocks(active, {cycle}, 116.0, 10.0);
  ASSERT_EQ(update.new_cycles.size(), 1U);
  EXPECT_EQ(update.new_cycles.front().robot_ids, cycle.robot_ids);
  EXPECT_TRUE(active.empty());
}

TEST(YieldUpstreamTest, Peeling_Workshop2WayAll8008SixRobotLayouts_ZeroPause) {
  const auto catalog = Workshop2WayCatalog();
  ASSERT_EQ(catalog.stations.size(), 16U);
  ASSERT_EQ(catalog.edges.size(), 24U);
  std::unordered_map<std::string, std::vector<std::string>> exits;
  std::unordered_map<std::string, std::vector<std::string>> entrances;
  const auto add_direction = [&](const std::string& from, const std::string& to) {
    exits[from].push_back(to);
    entrances[to].push_back(from);
  };
  for (const auto& edge : catalog.edges) {
    add_direction(edge.from, edge.to);
    if (edge.bidirectional) {
      add_direction(edge.to, edge.from);
    }
  }
  const std::vector<std::vector<std::string>> short_cycles{
      {"jn_n", "machining"}, {"jn_sw", "receiving"}, {"jn_sw", "park_1", "receiving"},
      {"jn_s", "machining"}, {"jn_n", "packing"}};
  StationOwners home_owners;
  for (int i = 1; i <= 6; ++i) {
    home_owners.emplace("park_" + std::to_string(i), "robot_" + std::to_string(i));
  }

  std::vector<int> occupied(catalog.stations.size(), 0);
  std::fill_n(occupied.begin(), 6, 1);
  std::size_t layouts = 0;
  std::size_t peels = 0;
  std::size_t pauses = 0;
  std::vector<std::size_t> closed_cases(short_cycles.size(), 0);
  do {
    ++layouts;
    StationOwners owners;
    for (std::size_t i = 0; i < occupied.size(); ++i) {
      if (occupied[i]) {
        const auto robot = "robot_" + std::to_string(owners.size() + 1);
        owners.emplace(catalog.stations[i].id, robot);
      }
    }
    for (std::size_t i = 0; i < short_cycles.size(); ++i) {
      const auto& ring = short_cycles[i];
      const bool closed = std::all_of(ring.begin(), ring.end(), [&](const auto& station) {
        return owners.count(station) != 0U &&
               std::all_of(exits.at(station).begin(), exits.at(station).end(),
                           [&](const auto& next) { return owners.count(next) != 0U; });
      });
      if (!closed) {
        continue;
      }
      ++closed_cases[i];
      SCOPED_TRACE(::testing::Message() << "layout " << layouts << ", ring " << i);
      StationWaits waits;
      for (std::size_t j = 0; j < ring.size(); ++j) {
        waits.emplace_back(ring[j], ring[(j + 1) % ring.size()]);
      }
      auto component_stations = ring;
      std::unordered_set<std::string> seen(ring.begin(), ring.end());
      // 沿有向入口逆向扩展。首个访问到的下游站给出到环的最短等待跳数。
      for (std::size_t j = 0; j < component_stations.size(); ++j) {
        const auto downstream = component_stations[j];
        for (const auto& from : entrances.at(downstream)) {
          if (owners.count(from) != 0U && seen.insert(from).second) {
            waits.emplace_back(from, downstream);
            component_stations.push_back(from);
          }
        }
      }
      const auto graph = BuildWorkshopWaitGraph(owners, waits);
      ASSERT_EQ(graph.cycles.size(), 1U);
      const auto& cycle = graph.cycles.front();
      ASSERT_EQ(cycle.robot_ids.size(), ring.size());
      const auto plan = PlanDeadlockResolution(
          cycle, graph.info, catalog, graph.locks, {}, graph.edges, 5.0, true, home_owners);
      EXPECT_EQ(plan.cycle_robot_ids, cycle.robot_ids);
      if (plan.action == "pause") {
        ++pauses;
        ADD_FAILURE() << plan.message;
        continue;
      }
      if (plan.peeling) {
        ++peels;
        ExpectClearWorkshopPath(plan, graph.locks);
      } else {
        std::unordered_set<std::string> component;
        for (const auto& station : component_stations) {
          component.insert(owners.at(station));
        }
        ExpectClearWorkshopPath(plan, graph.locks, &component);
      }
    }
  } while (std::prev_permutation(occupied.begin(), occupied.end()));

  EXPECT_EQ(layouts, 8008U);
  EXPECT_GT(peels, 0U);
  EXPECT_EQ(pauses, 0U);
  for (const auto count : closed_cases) {
    EXPECT_GT(count, 0U);
  }
  RecordProperty("layouts", static_cast<int>(layouts));
  RecordProperty("peels", static_cast<int>(peels));
  RecordProperty("pauses", static_cast<int>(pauses));
}

}  // namespace robot_tasks
