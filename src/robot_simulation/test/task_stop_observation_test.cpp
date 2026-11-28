#include "robot_simulation/task_stop_observation.hpp"

#include <gtest/gtest.h>

namespace robot_simulation {
namespace {

TEST(TaskStopObservationTest, RequiresOneSecondOfFreshStationarySamples) {
  TaskStopObservation observation;
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(observation.Observe(i * 0.1, 1.0, 2.0));
  }
  EXPECT_TRUE(observation.Observe(1.0, 1.0, 2.0));
}

TEST(TaskStopObservationTest, TraversalGapAndResetStartANewWindow) {
  TaskStopObservation observation;
  for (int i = 0; i < 10; ++i) {
    EXPECT_FALSE(observation.Observe(i * 0.1, 1.0, 2.0));
  }
  EXPECT_FALSE(observation.Observe(1.0, 1.002, 2.0));
  EXPECT_FALSE(observation.Observe(1.3, 1.002, 2.0));
  for (int i = 14; i < 23; ++i) {
    EXPECT_FALSE(observation.Observe(i * 0.1, 1.002, 2.0));
  }
  observation.Reset();
  EXPECT_FALSE(observation.Observe(2.3, 1.002, 2.0));
}

}  // namespace
}  // namespace robot_simulation
