#include "robot_tasks/traffic_reservation.hpp"

#include <gtest/gtest.h>

namespace robot_tasks {

TEST(TrafficReservationTest, RouteLockId_SortsEndpointsForSharedEdge) {
  EXPECT_EQ(RouteLockId("storage_b", "packing"), "route_edge:packing__storage_b");
  EXPECT_EQ(RouteLockId("packing", "storage_b"), "route_edge:packing__storage_b");
}

TEST(TrafficReservationTest, ParseRouteLock_ValidEdge_ReturnsEndpoints) {
  const auto parsed = ParseRouteLock("route_edge:packing__storage_b");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->first, "packing");
  EXPECT_EQ(parsed->second, "storage_b");
}

TEST(TrafficReservationTest, ParseRouteLock_InvalidResource_ReturnsNullopt) {
  EXPECT_FALSE(ParseRouteLock("route_node:packing").has_value());
  EXPECT_FALSE(ParseRouteLock("route_edge:packing").has_value());
  EXPECT_FALSE(ParseRouteLock("route_edge:__storage_b").has_value());
  EXPECT_FALSE(ParseRouteLock("route_edge:packing__").has_value());
}

TEST(TrafficReservationTest, BuildRouteLockIds_WithIntersectionLocks_AddsNodesAndEdges) {
  const auto lock_ids = BuildRouteLockIds({"receiving", "storage_a", "packing"}, true);

  ASSERT_EQ(lock_ids.size(), 5U);
  EXPECT_EQ(lock_ids[0], "route_node:receiving");
  EXPECT_EQ(lock_ids[1], "route_node:storage_a");
  EXPECT_EQ(lock_ids[2], "route_node:packing");
  EXPECT_EQ(lock_ids[3], "route_edge:receiving__storage_a");
  EXPECT_EQ(lock_ids[4], "route_edge:packing__storage_a");
}

TEST(TrafficReservationTest, BuildRouteLockIds_WithoutIntersectionLocks_AddsEdgesOnly) {
  const auto lock_ids = BuildRouteLockIds({"receiving", "storage_a", "packing"}, false);

  ASSERT_EQ(lock_ids.size(), 2U);
  EXPECT_EQ(lock_ids[0], "route_edge:receiving__storage_a");
  EXPECT_EQ(lock_ids[1], "route_edge:packing__storage_a");
}

TEST(TrafficReservationTest, IsTrafficBlockOwner_RecognizesOnlyTrafficBlocks) {
  EXPECT_TRUE(IsTrafficBlockOwner("traffic_block:maintenance"));
  EXPECT_FALSE(IsTrafficBlockOwner("mission_a"));
}

TEST(TrafficReservationTest, PlanTrafficBlock_UnknownStationRejects) {
  const auto decision = PlanTrafficBlock(
      TrafficBlockRequest{"receiving", "missing", "manual", false, false}, {});

  EXPECT_FALSE(decision.success);
  EXPECT_FALSE(decision.mutate_state);
  EXPECT_EQ(decision.resource_id, "route_edge:missing__receiving");
  EXPECT_EQ(
      decision.message, "unknown station in route block request: receiving -> missing");
}

TEST(TrafficReservationTest, PlanTrafficBlock_NoRouteRejects) {
  const auto decision = PlanTrafficBlock(
      TrafficBlockRequest{"receiving", "packing", "manual", true, false}, {});

  EXPECT_FALSE(decision.success);
  EXPECT_FALSE(decision.mutate_state);
  EXPECT_EQ(decision.message, "no enabled station route to block: receiving -> packing");
}

TEST(TrafficReservationTest, PlanTrafficBlock_ExistingLockRejects) {
  RouteLockMap route_locks{{"route_edge:receiving__storage_a", "mission_a"}};

  const auto decision = PlanTrafficBlock(
      TrafficBlockRequest{"receiving", "storage_a", "manual", true, true}, route_locks);

  EXPECT_FALSE(decision.success);
  EXPECT_FALSE(decision.mutate_state);
  EXPECT_EQ(
      decision.message,
      "route resource already reserved: route_edge:receiving__storage_a by mission_a");
}

TEST(TrafficReservationTest, PlanTrafficBlock_FreeRouteCreatesTrafficOwner) {
  const auto decision = PlanTrafficBlock(
      TrafficBlockRequest{"receiving", "storage_a", "aisle_maintenance", true, true}, {});

  EXPECT_TRUE(decision.success);
  EXPECT_TRUE(decision.mutate_state);
  EXPECT_EQ(decision.resource_id, "route_edge:receiving__storage_a");
  EXPECT_EQ(decision.owner_id, "traffic_block:aisle_maintenance");
  EXPECT_EQ(decision.message, "blocked route receiving -> storage_a");
}

TEST(TrafficReservationTest, PlanTrafficUnblock_MissingLockRejects) {
  const auto decision = PlanTrafficUnblock("receiving", "storage_a", {});

  EXPECT_FALSE(decision.success);
  EXPECT_FALSE(decision.mutate_state);
  EXPECT_EQ(decision.resource_id, "route_edge:receiving__storage_a");
  EXPECT_EQ(decision.message, "route resource not found: route_edge:receiving__storage_a");
}

TEST(TrafficReservationTest, PlanTrafficUnblock_MissionLockRejects) {
  RouteLockMap route_locks{{"route_edge:receiving__storage_a", "mission_a"}};

  const auto decision = PlanTrafficUnblock("receiving", "storage_a", route_locks);

  EXPECT_FALSE(decision.success);
  EXPECT_FALSE(decision.mutate_state);
  EXPECT_EQ(decision.owner_id, "mission_a");
  EXPECT_EQ(decision.message, "route resource is reserved by: mission_a");
}

TEST(TrafficReservationTest, PlanTrafficUnblock_TrafficBlockAllowsErase) {
  RouteLockMap route_locks{{"route_edge:receiving__storage_a", "traffic_block:manual"}};

  const auto decision = PlanTrafficUnblock("receiving", "storage_a", route_locks);

  EXPECT_TRUE(decision.success);
  EXPECT_TRUE(decision.mutate_state);
  EXPECT_EQ(decision.owner_id, "traffic_block:manual");
  EXPECT_EQ(decision.message, "unblocked route receiving -> storage_a");
}

TEST(TrafficReservationTest, PlanTrafficReservationClear_MissingRejects) {
  const auto decision = PlanTrafficReservationClear("route_edge:missing__route", {});

  EXPECT_FALSE(decision.success);
  EXPECT_FALSE(decision.erase_resource);
  EXPECT_TRUE(decision.release_mission_id.empty());
  EXPECT_EQ(decision.message, "traffic reservation not found: route_edge:missing__route");
}

TEST(TrafficReservationTest, PlanTrafficReservationClear_TrafficBlockErasesResource) {
  RouteLockMap route_locks{{"route_edge:receiving__storage_a", "traffic_block:manual"}};

  const auto decision =
      PlanTrafficReservationClear("route_edge:receiving__storage_a", route_locks);

  EXPECT_TRUE(decision.success);
  EXPECT_TRUE(decision.erase_resource);
  EXPECT_TRUE(decision.release_mission_id.empty());
  EXPECT_EQ(decision.owner_id, "traffic_block:manual");
  EXPECT_EQ(decision.message, "cleared blocked route route_edge:receiving__storage_a");
}

TEST(TrafficReservationTest, PlanTrafficReservationClear_MissionLockPlansMissionRelease) {
  RouteLockMap route_locks{{"route_edge:receiving__storage_a", "mission_a"}};

  const auto decision =
      PlanTrafficReservationClear("route_edge:receiving__storage_a", route_locks);

  EXPECT_TRUE(decision.success);
  EXPECT_FALSE(decision.erase_resource);
  EXPECT_EQ(decision.owner_id, "mission_a");
  EXPECT_EQ(decision.release_mission_id, "mission_a");
  EXPECT_EQ(
      decision.message, "cleared mission route reservation route_edge:receiving__storage_a");
}

TEST(TrafficReservationTest, ReserveRouteLocksForMission_FreeLocks_StoresReservation) {
  RouteLockMap route_locks;
  RouteLockReservationMap by_mission;
  std::string message;

  const bool reserved = ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_a",
      {"route_node:storage_a", "route_edge:packing__storage_a"}, &message);

  EXPECT_TRUE(reserved);
  EXPECT_EQ(message, "reserved 2 route lock(s)");
  EXPECT_EQ(route_locks["route_node:storage_a"], "mission_a");
  EXPECT_EQ(route_locks["route_edge:packing__storage_a"], "mission_a");
  ASSERT_EQ(by_mission.count("mission_a"), 1U);
  EXPECT_EQ(by_mission["mission_a"].route_lock_ids.size(), 2U);
}

TEST(TrafficReservationTest, ReserveRouteLocksForMission_SameMission_IsIdempotent) {
  RouteLockMap route_locks{
      {"route_node:storage_a", "mission_a"},
  };
  RouteLockReservationMap by_mission;
  std::string message;

  const bool reserved = ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_a", {"route_node:storage_a"}, &message);

  EXPECT_TRUE(reserved);
  EXPECT_EQ(route_locks["route_node:storage_a"], "mission_a");
  ASSERT_EQ(by_mission.count("mission_a"), 1U);
}

TEST(TrafficReservationTest, ReserveRouteLocksForMission_ConflictingOwner_RejectsReservation) {
  RouteLockMap route_locks{
      {"route_node:storage_a", "mission_existing"},
  };
  RouteLockReservationMap by_mission;
  std::string message;

  const bool reserved = ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_new", {"route_node:storage_a"}, &message);

  EXPECT_FALSE(reserved);
  EXPECT_EQ(message, "route resource locked: route_node:storage_a by mission_existing");
  EXPECT_EQ(route_locks["route_node:storage_a"], "mission_existing");
  EXPECT_EQ(by_mission.count("mission_new"), 0U);
}

TEST(TrafficReservationTest, ReleaseRouteLocksForMission_ExistingMission_RemovesOnlyOwnedLocks) {
  RouteLockMap route_locks{
      {"route_node:storage_a", "mission_a"},
      {"route_edge:packing__storage_a", "mission_a"},
      {"route_node:packing", "mission_b"},
  };
  RouteLockReservationMap by_mission{
      {"mission_a",
       RouteLockReservation{
           "mission_a", {"route_node:storage_a", "route_edge:packing__storage_a"}}},
  };

  const bool released = ReleaseRouteLocksForMission(route_locks, by_mission, "mission_a");

  EXPECT_TRUE(released);
  EXPECT_EQ(route_locks.count("route_node:storage_a"), 0U);
  EXPECT_EQ(route_locks.count("route_edge:packing__storage_a"), 0U);
  EXPECT_EQ(route_locks["route_node:packing"], "mission_b");
  EXPECT_EQ(by_mission.count("mission_a"), 0U);
}

TEST(TrafficReservationTest, ReleaseRouteLocksForMission_MissingMission_ReturnsFalse) {
  RouteLockMap route_locks{
      {"route_node:packing", "mission_b"},
  };
  RouteLockReservationMap by_mission;

  EXPECT_FALSE(ReleaseRouteLocksForMission(route_locks, by_mission, "missing"));
  EXPECT_EQ(route_locks["route_node:packing"], "mission_b");
}

TEST(TrafficReservationTest, ReleaseRouteLocksForMission_OrphanOwnedLocks_AreRemoved) {
  RouteLockMap route_locks{
      {"route_node:receiving", "mission_a"},
      {"route_edge:receiving__storage_a", "mission_a"},
      {"route_node:packing", "mission_b"},
  };
  RouteLockReservationMap by_mission;

  const bool released = ReleaseRouteLocksForMission(route_locks, by_mission, "mission_a");

  EXPECT_TRUE(released);
  EXPECT_EQ(route_locks.count("route_node:receiving"), 0U);
  EXPECT_EQ(route_locks.count("route_edge:receiving__storage_a"), 0U);
  EXPECT_EQ(route_locks["route_node:packing"], "mission_b");
}

// M3：whole_route 下站点运输以车辆号占用，按任务号记录。
TEST(TrafficReservationTest, ReserveRouteLocksForMission_OwnerIsRobot) {
  RouteLockMap route_locks;
  RouteLockReservationMap by_mission;
  std::string message;

  ASSERT_TRUE(ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_a", "robot_1",
      {"route_node:storage_a", "route_edge:packing__storage_a"}, &message));

  EXPECT_EQ(route_locks["route_node:storage_a"], "robot_1");
  EXPECT_EQ(route_locks["route_edge:packing__storage_a"], "robot_1");
  ASSERT_EQ(by_mission.count("mission_a"), 1U);
  EXPECT_EQ(by_mission["mission_a"].owner_id, "robot_1");
}

TEST(TrafficReservationTest, ReserveRouteLocksForMission_SameRobotTwoMissionsShareResources) {
  RouteLockMap route_locks;
  RouteLockReservationMap by_mission;
  std::string message;
  ASSERT_TRUE(ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_a", "robot_1",
      {"route_node:storage_a", "route_edge:packing__storage_a"}, &message));
  // 第二张工单与第一张共用 route_node:storage_a：同一辆车，不算冲突。
  ASSERT_TRUE(ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_b", "robot_1",
      {"route_node:storage_a", "route_edge:receiving__storage_a"}, &message));
  // 别的车撞上它们就拒绝，消息指出车辆号。
  EXPECT_FALSE(ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_c", "robot_2", {"route_node:storage_a"}, &message));
  EXPECT_EQ(message, "route resource locked: route_node:storage_a by robot_1");

  // 第一张结束：只放掉它独有的路段，共用的路口仍归 robot_1。
  EXPECT_TRUE(ReleaseRouteLocksForMission(route_locks, by_mission, "mission_a"));
  EXPECT_EQ(route_locks.count("route_edge:packing__storage_a"), 0U);
  EXPECT_EQ(route_locks["route_node:storage_a"], "robot_1");
  EXPECT_EQ(route_locks["route_edge:receiving__storage_a"], "robot_1");

  // 第二张也结束：全部放掉。
  EXPECT_TRUE(ReleaseRouteLocksForMission(route_locks, by_mission, "mission_b"));
  EXPECT_TRUE(route_locks.empty());
  EXPECT_TRUE(by_mission.empty());
}

TEST(TrafficReservationTest, ReleaseRouteLocksForMission_RobotOwner_KeepsRollingLocksOfSameRobot) {
  RouteLockMap route_locks;
  RouteLockReservationMap by_mission;
  std::string message;
  ASSERT_TRUE(ReserveRouteLocksForMission(
      route_locks, by_mission, "mission_a", "robot_1", {"route_node:storage_a"}, &message));
  route_locks["route_node:packing"] = "robot_1";  // 不属于这张工单

  EXPECT_TRUE(ReleaseRouteLocksForMission(route_locks, by_mission, "mission_a"));
  EXPECT_EQ(route_locks.count("route_node:storage_a"), 0U);
  EXPECT_EQ(route_locks["route_node:packing"], "robot_1");
}

// ---------------------------------------------------------------------------
// M2 滚动申请 + 等待队列
// ---------------------------------------------------------------------------

namespace {

RouteSegmentRequest SegmentRequest(
    const std::string& robot_id, const std::string& from, const std::string& to,
    const int priority = 0) {
  RouteSegmentRequest request;
  request.robot_id = robot_id;
  request.from_station = from;
  request.to_station = to;
  request.priority = priority;
  request.resource_ids = BuildRouteSegmentResourceIds(from, to, true);
  return request;
}

}  // namespace

TEST(RouteSegmentTest, BuildRouteSegmentResourceIds_EdgeAndNextNode) {
  EXPECT_EQ(
      BuildRouteSegmentResourceIds("receiving", "storage_a", true),
      (std::vector<std::string>{"route_edge:receiving__storage_a", "route_node:storage_a"}));
  EXPECT_EQ(
      BuildRouteSegmentResourceIds("receiving", "storage_a", false),
      (std::vector<std::string>{"route_edge:receiving__storage_a"}));
  EXPECT_EQ(
      BuildRouteSegmentResourceIds("", "dock", true),
      (std::vector<std::string>{"route_node:dock"}));
}

TEST(RouteSegmentTest, Request_FreeResources_Granted) {
  RouteLockMap locks;
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;

  const auto decision =
      PlanRouteSegmentRequest(SegmentRequest("robot_1", "receiving", "storage_a"), locks, queues, &seq);

  EXPECT_TRUE(decision.granted);
  EXPECT_FALSE(decision.queued);
  EXPECT_EQ(locks["route_edge:receiving__storage_a"], "robot_1");
  EXPECT_EQ(locks["route_node:storage_a"], "robot_1");
  EXPECT_TRUE(queues.empty());
}

TEST(RouteSegmentTest, Request_AlreadyOwnedBySameRobot_Granted) {
  RouteLockMap locks{{"route_node:storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;

  const auto decision =
      PlanRouteSegmentRequest(SegmentRequest("robot_1", "receiving", "storage_a"), locks, queues, &seq);

  EXPECT_TRUE(decision.granted);
}

TEST(RouteSegmentTest, Request_OccupiedResource_Queued) {
  RouteLockMap locks{{"route_node:storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;

  const auto decision =
      PlanRouteSegmentRequest(SegmentRequest("robot_2", "packing", "storage_a"), locks, queues, &seq);

  EXPECT_FALSE(decision.granted);
  EXPECT_TRUE(decision.queued);
  EXPECT_EQ(decision.blocking_resource_ids, (std::vector<std::string>{"route_node:storage_a"}));
  EXPECT_EQ(decision.blocking_owner_ids, (std::vector<std::string>{"robot_1"}));
  // 拿不到的那组资源一个都不锁。
  EXPECT_EQ(locks.count("route_edge:packing__storage_a"), 0U);
  ASSERT_EQ(queues["route_node:storage_a"].size(), 1U);
  EXPECT_EQ(queues["route_node:storage_a"][0].robot_id, "robot_2");
  const auto waiter = FindRouteWaiter("robot_2", queues);
  ASSERT_TRUE(waiter.has_value());
  EXPECT_EQ(waiter->to_station, "storage_a");
}

TEST(RouteSegmentTest, Request_WholeRouteMode_RejectsWithoutQueue) {
  RouteLockMap locks{{"route_node:storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  auto request = SegmentRequest("robot_2", "packing", "storage_a");
  request.allow_queue = false;

  const auto decision = PlanRouteSegmentRequest(request, locks, queues, &seq);

  EXPECT_FALSE(decision.granted);
  EXPECT_FALSE(decision.queued);
  EXPECT_EQ(decision.message, "route resource locked: route_node:storage_a held by robot_1");
  EXPECT_TRUE(queues.empty());
}

TEST(RouteSegmentTest, Release_GrantsQueueHead) {
  RouteLockMap locks;
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  ASSERT_TRUE(
      PlanRouteSegmentRequest(SegmentRequest("robot_1", "receiving", "storage_a"), locks, queues, &seq)
          .granted);
  ASSERT_TRUE(
      PlanRouteSegmentRequest(SegmentRequest("robot_2", "packing", "storage_a"), locks, queues, &seq)
          .queued);
  ASSERT_TRUE(
      PlanRouteSegmentRequest(SegmentRequest("robot_3", "receiving", "storage_a"), locks, queues, &seq)
          .queued);

  const auto release = PlanRouteSegmentRelease("robot_1", {}, locks, queues);

  EXPECT_EQ(release.released_resource_ids.size(), 2U);
  ASSERT_EQ(release.granted.size(), 1U);
  EXPECT_EQ(release.granted[0].robot_id, "robot_2");
  EXPECT_EQ(locks["route_node:storage_a"], "robot_2");
  EXPECT_EQ(locks["route_edge:packing__storage_a"], "robot_2");
  EXPECT_FALSE(FindRouteWaiter("robot_2", queues).has_value());
  // robot_3 仍在等 robot_2 刚拿走的路口。
  const auto still_waiting = FindRouteWaiter("robot_3", queues);
  ASSERT_TRUE(still_waiting.has_value());
}

TEST(RouteSegmentTest, Release_OnlyNamedResources) {
  RouteLockMap locks{
      {"route_edge:receiving__storage_a", "robot_1"},
      {"route_node:storage_a", "robot_1"},
      {"route_node:receiving", "robot_1"}};
  RouteWaitQueueMap queues;

  const auto release =
      PlanRouteSegmentRelease("robot_1", {"route_edge:receiving__storage_a", "route_node:receiving",
                                          "route_node:packing"},
                              locks, queues);

  EXPECT_EQ(release.released_resource_ids.size(), 2U);
  EXPECT_EQ(locks.size(), 1U);
  EXPECT_EQ(locks["route_node:storage_a"], "robot_1");
}

TEST(RouteSegmentTest, Release_SkipsWaiterThatStillCannotGetAllResources) {
  RouteLockMap locks{
      {"route_node:storage_a", "robot_1"},
      {"route_edge:packing__storage_a", "robot_9"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  // robot_2 要的两个资源都被占；robot_3 只要路口。
  ASSERT_TRUE(
      PlanRouteSegmentRequest(SegmentRequest("robot_2", "packing", "storage_a"), locks, queues, &seq)
          .queued);
  ASSERT_TRUE(
      PlanRouteSegmentRequest(SegmentRequest("robot_3", "", "storage_a"), locks, queues, &seq).queued);

  const auto release = PlanRouteSegmentRelease("robot_1", {}, locks, queues);

  ASSERT_EQ(release.granted.size(), 1U);
  EXPECT_EQ(release.granted[0].robot_id, "robot_3");
  EXPECT_EQ(locks["route_node:storage_a"], "robot_3");
  EXPECT_TRUE(FindRouteWaiter("robot_2", queues).has_value());
}

TEST(RouteSegmentTest, Queue_HigherPriorityJumpsAhead) {
  RouteLockMap locks{{"route_node:storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(SegmentRequest("robot_2", "", "storage_a", 10), locks, queues, &seq);
  PlanRouteSegmentRequest(SegmentRequest("robot_3", "", "storage_a", 50), locks, queues, &seq);
  PlanRouteSegmentRequest(SegmentRequest("robot_4", "", "storage_a", 10), locks, queues, &seq);

  const auto& queue = queues["route_node:storage_a"];
  ASSERT_EQ(queue.size(), 3U);
  EXPECT_EQ(queue[0].robot_id, "robot_3");
  EXPECT_EQ(queue[1].robot_id, "robot_2");
  EXPECT_EQ(queue[2].robot_id, "robot_4");

  const auto release = PlanRouteSegmentRelease("robot_1", {}, locks, queues);
  ASSERT_EQ(release.granted.size(), 1U);
  EXPECT_EQ(release.granted[0].robot_id, "robot_3");
}

TEST(RouteSegmentTest, Queue_RepeatedRequestKeepsPlaceAndReplacesOldWait) {
  RouteLockMap locks{{"route_node:storage_a", "robot_1"}, {"route_node:packing", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(SegmentRequest("robot_2", "", "storage_a"), locks, queues, &seq);
  PlanRouteSegmentRequest(SegmentRequest("robot_3", "", "storage_a"), locks, queues, &seq);
  // 重复同一申请：排位不变，也不算新排队。
  const auto repeated =
      PlanRouteSegmentRequest(SegmentRequest("robot_2", "", "storage_a"), locks, queues, &seq);
  EXPECT_TRUE(repeated.queued);
  EXPECT_FALSE(repeated.newly_queued);
  EXPECT_EQ(queues["route_node:storage_a"][0].robot_id, "robot_2");

  // 改申请另一组资源：旧等待被撤掉。
  PlanRouteSegmentRequest(SegmentRequest("robot_2", "", "packing"), locks, queues, &seq);
  ASSERT_EQ(queues["route_node:storage_a"].size(), 1U);
  EXPECT_EQ(queues["route_node:storage_a"][0].robot_id, "robot_3");
  ASSERT_EQ(queues["route_node:packing"].size(), 1U);
}

TEST(RouteSegmentTest, Block_RequestOnBlockedRouteNeedsReplan) {
  RouteLockMap locks{{"route_edge:receiving__storage_a", TrafficBlockOwnerId("maintenance")}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;

  const auto blocked =
      PlanRouteSegmentRequest(SegmentRequest("robot_2", "receiving", "storage_a"), locks, queues, &seq);

  EXPECT_FALSE(blocked.granted);
  EXPECT_FALSE(blocked.queued);
  EXPECT_TRUE(blocked.replan_required);
  EXPECT_TRUE(queues.empty());
  EXPECT_EQ(locks.count("route_node:storage_a"), 0U);
}

TEST(RouteSegmentTest, Block_QueuedWaitersMarkedForReplan) {
  RouteLockMap locks{{"route_edge:receiving__storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(SegmentRequest("robot_2", "receiving", "storage_a"), locks, queues, &seq);
  PlanRouteSegmentRequest(SegmentRequest("robot_3", "receiving", "storage_a"), locks, queues, &seq);

  const auto replan = PlanRouteBlockWaiterReplan("route_edge:receiving__storage_a", queues);

  ASSERT_EQ(replan.size(), 2U);
  EXPECT_EQ(replan[0].robot_id, "robot_2");
  EXPECT_EQ(replan[1].robot_id, "robot_3");
  EXPECT_TRUE(queues.empty());
}

TEST(RouteSegmentTest, Withdraw_RemovesWaiterEverywhere) {
  RouteLockMap locks{{"route_edge:receiving__storage_a", "robot_1"}, {"route_node:storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(SegmentRequest("robot_2", "receiving", "storage_a"), locks, queues, &seq);
  ASSERT_EQ(queues.size(), 2U);

  const auto withdrawn = WithdrawRouteWaits("robot_2", queues);

  ASSERT_TRUE(withdrawn.has_value());
  EXPECT_TRUE(queues.empty());
  EXPECT_FALSE(WithdrawRouteWaits("robot_2", queues).has_value());
}

TEST(RouteSegmentTest, ResourcesOwnedBy_Sorted) {
  RouteLockMap locks{
      {"route_node:storage_a", "robot_1"},
      {"route_edge:a__b", "robot_1"},
      {"route_node:packing", "robot_2"}};
  EXPECT_EQ(
      RouteResourcesOwnedBy("robot_1", locks),
      (std::vector<std::string>{"route_edge:a__b", "route_node:storage_a"}));
}

TEST(RouteSegmentTest, BuildRoutePathResourceIds_EdgesAndNodesExceptStart) {
  EXPECT_EQ(
      BuildRoutePathResourceIds({"dock", "receiving", "storage_a"}, true),
      (std::vector<std::string>{
          "route_edge:dock__receiving", "route_edge:receiving__storage_a",
          "route_node:receiving", "route_node:storage_a"}));
  EXPECT_EQ(
      BuildRoutePathResourceIds({"dock", "receiving"}, false),
      (std::vector<std::string>{"route_edge:dock__receiving"}));
}

TEST(RouteSegmentTest, WaitGrants_AllQueues_GrantsWaiterWhoseResourcesFreedElsewhere) {
  RouteLockMap locks{{"route_node:storage_a", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(SegmentRequest("robot_2", "", "storage_a"), locks, queues, &seq);
  // 资源被别的途径（清除预约、任务清理）删掉，没经过 PlanRouteSegmentRelease。
  locks.erase("route_node:storage_a");

  const auto granted = PlanRouteWaitGrants({}, locks, queues);

  ASSERT_EQ(granted.size(), 1U);
  EXPECT_EQ(granted[0].robot_id, "robot_2");
  EXPECT_EQ(locks["route_node:storage_a"], "robot_2");
  EXPECT_TRUE(queues.empty());
}

// ---------------------------------------------------------------------------
// M3 防饿死：等待过久的等待者暂扣它所需的空闲资源
// ---------------------------------------------------------------------------

namespace {

RouteSegmentRequest TimedRequest(
    const std::string& robot_id, const std::vector<std::string>& resources, const int priority,
    const double now_s) {
  RouteSegmentRequest request;
  request.robot_id = robot_id;
  request.from_station = "a";
  request.to_station = "b";
  request.priority = priority;
  request.resource_ids = resources;
  request.now_s = now_s;
  return request;
}

}  // namespace

TEST(RouteStarvationTest, SkippedWaiterBelowThreshold_LowerPriorityStillGranted) {
  RouteLockMap locks{{"route_edge:a__b", "robot_9"}, {"route_node:b", "robot_8"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  // robot_3（优先级 20）要路段 + 路口，两样都被占；robot_2（优先级 10）只要路口。
  PlanRouteSegmentRequest(
      TimedRequest("robot_3", {"route_edge:a__b", "route_node:b"}, 20, 0.0), locks, queues, &seq);
  PlanRouteSegmentRequest(TimedRequest("robot_2", {"route_node:b"}, 10, 1.0), locks, queues, &seq);
  locks.erase("route_node:b");

  RouteHoldMap holds;
  RouteWaitGrantOptions options;
  options.now_s = 10.0;  // robot_3 等了 10 秒，未超过 20 秒阈值
  options.starvation_s = 20.0;
  const auto granted = PlanRouteWaitGrants({"route_node:b"}, locks, queues, options, &holds);

  ASSERT_EQ(granted.size(), 1U);
  EXPECT_EQ(granted[0].robot_id, "robot_2");
  EXPECT_TRUE(holds.empty());
  EXPECT_TRUE(FindRouteWaiter("robot_3", queues).has_value());
}

TEST(RouteStarvationTest, StarvingWaiter_HoldsFreedResourceAgainstLowerPriority) {
  RouteLockMap locks{{"route_edge:a__b", "robot_9"}, {"route_node:jn_s", "robot_8"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(
      TimedRequest("robot_3", {"route_edge:a__b", "route_node:jn_s"}, 20, 0.0), locks, queues,
      &seq);
  PlanRouteSegmentRequest(TimedRequest("robot_2", {"route_node:jn_s"}, 10, 1.0), locks, queues, &seq);
  locks.erase("route_node:jn_s");

  RouteHoldMap holds;
  RouteWaitGrantOptions options;
  options.now_s = 25.0;  // robot_3 已等 25 秒
  options.starvation_s = 20.0;
  const auto granted = PlanRouteWaitGrants({"route_node:jn_s"}, locks, queues, options, &holds);

  EXPECT_TRUE(granted.empty());
  EXPECT_EQ(locks.count("route_node:jn_s"), 0U);  // 保持空闲
  ASSERT_EQ(holds.count("route_node:jn_s"), 1U);
  EXPECT_EQ(holds["route_node:jn_s"].robot_id, "robot_3");
  EXPECT_TRUE(FindRouteWaiter("robot_2", queues).has_value());

  // 新申请撞上暂扣也排队，阻塞者记为暂扣者。
  const auto late = PlanRouteSegmentRequest(
      TimedRequest("robot_4", {"route_node:jn_s"}, 5, 26.0), locks, queues, &seq, &holds);
  EXPECT_FALSE(late.granted);
  EXPECT_TRUE(late.queued);
  ASSERT_EQ(late.blocking_owner_ids.size(), 1U);
  EXPECT_EQ(late.blocking_owner_ids[0], "robot_3");

  // 更高优先级的新申请不受暂扣约束。
  const auto urgent = PlanRouteSegmentRequest(
      TimedRequest("robot_5", {"route_node:jn_s"}, 30, 26.0), locks, queues, &seq, &holds);
  EXPECT_TRUE(urgent.granted);
}

TEST(RouteStarvationTest, RepeatedRequestKeepsWaitStart) {
  RouteLockMap locks{{"route_node:b", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(TimedRequest("robot_2", {"route_node:b"}, 0, 3.0), locks, queues, &seq);
  PlanRouteSegmentRequest(TimedRequest("robot_2", {"route_node:b"}, 0, 9.0), locks, queues, &seq);

  const auto waiter = FindRouteWaiter("robot_2", queues);
  ASSERT_TRUE(waiter.has_value());
  EXPECT_DOUBLE_EQ(waiter->since_s, 3.0);
}

TEST(RouteStarvationTest, FrozenRobotIsNeitherGrantedNorHolding) {
  RouteLockMap locks{{"route_node:b", "robot_1"}};
  RouteWaitQueueMap queues;
  std::uint64_t seq = 1;
  PlanRouteSegmentRequest(TimedRequest("robot_2", {"route_node:b"}, 0, 0.0), locks, queues, &seq);
  locks.clear();

  const std::unordered_set<std::string> frozen{"robot_2"};
  RouteWaitGrantOptions options;
  options.now_s = 100.0;
  options.starvation_s = 20.0;
  options.frozen_robot_ids = &frozen;
  RouteHoldMap holds;
  const auto granted = PlanRouteWaitGrants({}, locks, queues, options, &holds);

  EXPECT_TRUE(granted.empty());
  EXPECT_TRUE(holds.empty());
  EXPECT_TRUE(FindRouteWaiter("robot_2", queues).has_value());
}

}  // namespace robot_tasks
