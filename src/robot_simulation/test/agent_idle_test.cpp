#include "robot_simulation/agent_idle.hpp"

#include <gtest/gtest.h>

namespace robot_simulation {
namespace {

TEST(AgentIdleTest, FreshAgentAwayFromHomeStaysPut) {
  // 刚启动、还没完成过任务：即使不在停车位也原地等待（场景布置靠这一点）。
  EXPECT_FALSE(ShouldReturnHomeWhenIdle(true, false, true, false, "jn_s", "park_1"));
}

TEST(AgentIdleTest, AfterFirstTaskIdleAgentReturnsHome) {
  EXPECT_TRUE(ShouldReturnHomeWhenIdle(true, false, true, true, "packing", "park_4"));
}

TEST(AgentIdleTest, AlreadyHomeOrBusyOrDisabledDoesNotMove) {
  EXPECT_FALSE(ShouldReturnHomeWhenIdle(true, false, true, true, "park_4", "park_4"));
  EXPECT_FALSE(ShouldReturnHomeWhenIdle(true, true, true, true, "packing", "park_4"));
  EXPECT_FALSE(ShouldReturnHomeWhenIdle(true, false, false, true, "packing", "park_4"));
  EXPECT_FALSE(ShouldReturnHomeWhenIdle(false, false, true, true, "packing", "park_4"));
}

}  // namespace
}  // namespace robot_simulation
