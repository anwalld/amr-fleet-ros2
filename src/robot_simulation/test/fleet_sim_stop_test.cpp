#include <gtest/gtest.h>

#include "robot_simulation/fleet_sim_stop.hpp"

namespace robot_simulation {

TEST(FleetSimStopTest, MotionRequiresFreshReleasedState) {
  EXPECT_TRUE(FleetSimMotionStopped(true, 0.0, 0.0));
  EXPECT_TRUE(FleetSimMotionStopped(true, 1.0, 1.2));
  EXPECT_FALSE(FleetSimMotionStopped(false, 1.0, 2.9));
  EXPECT_TRUE(FleetSimMotionStopped(false, 1.0, 3.0));
  EXPECT_FALSE(FleetSimMotionStopped(false, 3.1, 3.2));
}

}  // namespace robot_simulation
