#include "robot_tasks/mission_restart_store.hpp"

#include <cstdlib>
#include <gtest/gtest.h>

namespace robot_tasks {
namespace {

class MissionRestartStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    char path[] = "/tmp/robot-mission-restart.XXXXXX";
    const char* created = mkdtemp(path);
    ASSERT_NE(created, nullptr);
    scratch_ = created;
  }

  void TearDown() override {
    std::error_code error;
    if (!scratch_.empty()) {
      std::filesystem::remove_all(scratch_, error);
    }
  }

  std::filesystem::path scratch_;
};

TEST_F(MissionRestartStoreTest, ReconstructsMissionAndRemoteIdentities) {
  using Start = MissionRestartStore::Start;
  const auto path = scratch_ / "checkpoint";
  {
    MissionRestartStore owner;
    ASSERT_EQ(owner.Open(path), Start::kFresh);
    MissionRestartStore overlapping;
    EXPECT_EQ(overlapping.Open(path), Start::kUnavailable);
    MissionRestartSnapshot snapshot;
    snapshot.robot_id = "dispatcher";
    snapshot.mission_id = "old mission";
    snapshot.goal_uuid = "0123456789abcdef0123456789abcdef";
    snapshot.pause_mask = 5;
    snapshot.pause_revision = 13;
    snapshot.mission_busy = true;
    snapshot.dispatch_held = true;
    snapshot.queue.push_back({{"new mission", "map", false, {{2.51, 8, -0.3}}},
                              "generated profile", 42, 91});
    snapshot.paused_workflows.insert("work order");
    snapshot.remote_tasks.emplace("remote task", RemoteFleetTaskState{
        "remote task", "robot_1", "dock", "packing", "PAUSED", "ready", 7});
    snapshot.receipts.emplace(std::make_tuple("remote task", 19, 6),
                              RestartReceipt{true, "paused", 7, "PAUSED"});
    ASSERT_TRUE(owner.Save(snapshot));
  }

  MissionRestartStore restarted;
  ASSERT_EQ(restarted.Open(path), Start::kExisting);
  const auto saved = restarted.Read();
  ASSERT_TRUE(saved);
  EXPECT_EQ(saved->goal_uuid, "0123456789abcdef0123456789abcdef");
  EXPECT_EQ(saved->pause_revision, 13U);
  EXPECT_EQ(saved->pause_mask, 5U);
  EXPECT_TRUE(saved->dispatch_held);
  ASSERT_EQ(saved->queue.size(), 1U);
  EXPECT_EQ(saved->queue[0].sequence, 91U);
  EXPECT_DOUBLE_EQ(saved->queue[0].profile.waypoints.at(0).x, 2.51);
  EXPECT_EQ(saved->paused_workflows.count("work order"), 1U);
  EXPECT_EQ(saved->remote_tasks.at("remote task").assigned_robot_id, "robot_1");
  EXPECT_EQ(saved->receipts.at(std::make_tuple("remote task", 19, 6)).latest_revision, 7U);
}

TEST_F(MissionRestartStoreTest, DoesNotReadCorruptOrPartialSnapshot) {
  using Start = MissionRestartStore::Start;
  const auto path = scratch_ / "checkpoint";
  MissionRestartStore owner;
  ASSERT_EQ(owner.Open(path), Start::kFresh);
  EXPECT_FALSE(owner.Read());
  std::ofstream(path / "mission-v1") << "mission-v1\ndispatcher mission 012345";
  EXPECT_FALSE(owner.Read());
}

}  // namespace
}  // namespace robot_tasks
