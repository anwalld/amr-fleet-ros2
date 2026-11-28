#include "robot_simulation/agent_lease.hpp"

#include <gtest/gtest.h>

namespace robot_simulation {

TEST(AgentLeaseTest, IgnoresStaleEpochAndRevisionNoticesAndAcksExactBatch) {
  AgentLeaseTracker tracker;
  const std::uint64_t e1 = 1001;
  const std::uint64_t e2 = 2002;

  ASSERT_TRUE(tracker.AcceptRequestResponse(e1, true, {"route_node:a"}, {7}));
  ASSERT_TRUE(tracker.AcceptRequestResponse(e2, true, {"route_node:a"}, {2}));
  EXPECT_EQ(tracker.active_service_epoch(), e2);

  EXPECT_FALSE(tracker.CanHintGrant(e1));
  EXPECT_FALSE(tracker.IsCurrentEpoch(e1));
  EXPECT_TRUE(tracker.IsCurrentEpoch(e2));
  EXPECT_FALSE(tracker.AcceptRequestResponse(e1, true, {"route_node:a"}, {99}));
  EXPECT_FALSE(tracker.AcceptUnneededGrant(e1, "route_node:a", 99));
  EXPECT_FALSE(tracker.AcceptUnneededGrant(e2, "route_node:a", 1));
  EXPECT_FALSE(tracker.AcceptUnneededGrant(e2, "route_node:a", 2));
  EXPECT_EQ(tracker.held_leases().at("route_node:a"), (LeaseToken{e2, 2}));

  EXPECT_TRUE(tracker.AcceptUnneededGrant(e2, "route_edge:a__b", 5));
  EXPECT_TRUE(tracker.AcceptUnneededGrant(e2, "route_node:b", 3));
  EXPECT_EQ(tracker.held_leases().at("route_edge:a__b"), (LeaseToken{e2, 5}));
  EXPECT_EQ(tracker.held_leases().at("route_node:b"), (LeaseToken{e2, 3}));

  // 6. 释放应答按 (epoch, revision) 精确核销：若期间已替换为新令牌，旧应答不得误核销。
  tracker.QueueRelease({"route_node:a"});
  const std::vector<std::pair<std::string, LeaseToken>> stale_batch{
      {"route_node:a", LeaseToken{e2, 1}}};
  tracker.AcknowledgeReleaseBatch(stale_batch);
  EXPECT_EQ(tracker.pending_releases().size(), 1U);

  const std::vector<std::pair<std::string, LeaseToken>> exact_batch{
      {"route_node:a", LeaseToken{e2, 2}}};
  tracker.AcknowledgeReleaseBatch(exact_batch);
  EXPECT_TRUE(tracker.pending_releases().empty());
}

TEST(AgentLeaseTest, ReleasingCannotRestoreAnOldRevisionOrOverwritePendingToken) {
  AgentLeaseTracker tracker;
  ASSERT_TRUE(tracker.AcceptRequestResponse(7, true, {"a"}, {4}));
  EXPECT_TRUE(tracker.AcceptRequestResponse(7, true, {"a"}, {4}));
  tracker.QueueRelease({"a"});
  tracker.QueueRelease({"a"});
  EXPECT_FALSE(tracker.AcceptRequestResponse(7, true, {"a"}, {4}));
  EXPECT_FALSE(tracker.AcceptRequestResponse(7, true, {"a"}, {3}));
  EXPECT_EQ(tracker.pending_releases().at("a"), (LeaseToken{7, 4}));

  ASSERT_TRUE(tracker.AcceptRequestResponse(8, false, {}, {}));
  EXPECT_TRUE(tracker.held().empty());
  EXPECT_EQ(tracker.pending_releases().at("a"), (LeaseToken{7, 4}));
  tracker.AcknowledgeReleaseBatch({{"a", {7, 4}}});
  EXPECT_TRUE(tracker.pending_releases().empty());
  EXPECT_TRUE(tracker.AcceptRequestResponse(8, true, {"a"}, {1}));
}

TEST(AgentLeaseTest, WaitingHintNeverCommitsAnUnknownEpoch) {
  AgentLeaseTracker tracker;
  ASSERT_TRUE(tracker.AcceptRequestResponse(7, false, {}, {}));
  EXPECT_TRUE(tracker.CanHintGrant(8));
  EXPECT_FALSE(tracker.IsCurrentEpoch(8));
  EXPECT_FALSE(tracker.AcceptUnneededGrant(8, "a", 1));
  EXPECT_TRUE(tracker.held().empty());
  EXPECT_FALSE(tracker.AcceptRequestResponse(0, true, {"a"}, {1}));
  EXPECT_FALSE(tracker.AcceptRequestResponse(7, true, {"a"}, {}));
}

TEST(AgentLeaseTest, FirstServiceEpochPreservesReconciledStationUntilRelease) {
  AgentLeaseTracker tracker;
  tracker.RestoreReconciledResources({"route_node:a"});
  EXPECT_EQ(tracker.active_service_epoch(), 0U);
  EXPECT_TRUE(tracker.held_leases().empty());

  ASSERT_TRUE(tracker.AcceptRequestResponse(
      7, true, {"route_edge:a__b", "route_node:b"}, {1, 2}));
  EXPECT_EQ(tracker.held(),
            (std::set<std::string>{"route_node:a", "route_edge:a__b", "route_node:b"}));
  EXPECT_EQ(tracker.held_leases().count("route_node:a"), 0U);
  tracker.QueueRelease({"route_node:a", "route_edge:a__b"});
  EXPECT_EQ(tracker.pending_releases().at("route_node:a"), LeaseToken{});
  EXPECT_EQ(tracker.pending_releases().at("route_edge:a__b"), (LeaseToken{7, 1}));
  EXPECT_EQ(tracker.held(), (std::set<std::string>{"route_node:b"}));

  ASSERT_TRUE(tracker.AcceptRequestResponse(8, false, {}, {}));
  EXPECT_TRUE(tracker.held().empty());
  EXPECT_TRUE(tracker.held_leases().empty());
  EXPECT_FALSE(tracker.CanHintGrant(7));
}

TEST(AgentLeaseTest, RestoringReconciledResourcesReplacesHeldResourcesAndClearsTokens) {
  AgentLeaseTracker tracker;
  ASSERT_TRUE(tracker.AcceptRequestResponse(7, true, {"route_node:b"}, {2}));

  tracker.RestoreReconciledResources({"route_node:a"});
  EXPECT_EQ(tracker.held(), (std::set<std::string>{"route_node:a"}));
  EXPECT_TRUE(tracker.held_leases().empty());
}

}  // namespace robot_simulation
