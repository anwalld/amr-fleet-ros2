#include "robot_simulation/parked_task_checkpoint.hpp"

#include <cstdlib>
#include <gtest/gtest.h>

namespace robot_simulation {
namespace {

class ParkedTaskCheckpointTest : public testing::Test {
 protected:
  void SetUp() override {
    char path[] = "/tmp/robot-parked-task.XXXXXX";
    const char* created = mkdtemp(path);
    ASSERT_NE(created, nullptr);
    scratch_ = created;
    checkpoint_ = scratch_ / "checkpoint";
  }

  void TearDown() override {
    std::error_code error;
    if (!scratch_.empty()) {
      std::filesystem::remove_all(scratch_, error);
    }
  }

  std::filesystem::path scratch_;
  std::filesystem::path checkpoint_;
};

TEST_F(ParkedTaskCheckpointTest, ExclusiveOwnerAndRestart) {
  using Start = ParkedTaskCheckpointStore::Start;
  {
    ParkedTaskCheckpointStore first;
    ASSERT_EQ(first.Open(checkpoint_), Start::kFresh);
    ParkedTaskCheckpointStore second;
    EXPECT_EQ(second.Open(checkpoint_), Start::kUnavailable);
    EXPECT_FALSE(second.Read());
    ParkedTaskCheckpoint value{"robot_1", "rolling", "task with spaces",
                               "park_1", "receiving", 23, 2, 4, 1.2, 3.4, -0.5,
                               {"route_edge:park_1__receiving", "route_node:receiving"}, true, "traffic-endpoint", std::nullopt};
    ASSERT_TRUE(first.Save(value));
  }

  ParkedTaskCheckpointStore restarted;
  ASSERT_EQ(restarted.Open(checkpoint_), Start::kExisting);
  const auto stored = restarted.Read();
  ASSERT_TRUE(stored);
  EXPECT_EQ(stored->task_id, "task with spaces");
  EXPECT_EQ(stored->latest_revision, 4U);
  EXPECT_EQ(stored->stop_revision, 2U);
  EXPECT_EQ(stored->execution, 23U);
  EXPECT_DOUBLE_EQ(stored->x, 1.2);
  EXPECT_TRUE(stored->completed_task);
  EXPECT_EQ(stored->resources.size(), 2U);
  EXPECT_EQ(stored->traffic_authority, "traffic-endpoint");
  ASSERT_TRUE(restarted.Invalidate());
  EXPECT_FALSE(restarted.Read());
}

TEST_F(ParkedTaskCheckpointTest, RecoversPausedSegmentAndRejectsInvalidIndex) {
  using Start = ParkedTaskCheckpointStore::Start;
  ParkedTaskCheckpointStore store;
  ASSERT_EQ(store.Open(checkpoint_), Start::kFresh);
  ParkedTaskCheckpoint value{"robot_1", "rolling", "task", "park_1", "receiving", 23, 2, 4,
      1.2, 3.4, -0.5, {"route_edge:park_1__receiving"}, true, "traffic-endpoint",
      PausedTaskProgress{true, "receiving", "assembly", 10, {"park_1", "receiving"}, 1, 0}};
  ASSERT_TRUE(store.Save(value));
  auto restored = store.Read();
  ASSERT_TRUE(restored && restored->paused);
  EXPECT_EQ(restored->paused->path, value.paused->path);
  EXPECT_EQ(restored->paused->path_index, 1U);
  EXPECT_EQ(restored->paused->pickup, "receiving");
  value.paused->path_index = value.paused->path.size();
  ASSERT_TRUE(store.Save(value));
  EXPECT_FALSE(store.Read());
}

TEST_F(ParkedTaskCheckpointTest, TruncatedRecordNeverRecovers) {
  using Start = ParkedTaskCheckpointStore::Start;
  ParkedTaskCheckpointStore store;
  ASSERT_EQ(store.Open(checkpoint_), Start::kFresh);
  std::ofstream(checkpoint_ / "parked") << "parked-task-v2\nrobot_1 rolling task";
  EXPECT_FALSE(store.Read());
}

}  // namespace
}  // namespace robot_simulation
