#include "robot_simulation/agent_yield.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace robot_simulation {
namespace {

using Path = std::vector<std::string>;

const Path kBackoff{"jn_n", "machining", "jn_s", "park_2"};
// 夜跑 2026-11-18-r2、2026-11-19-rework2 两次崩溃前收到的改道路径。
const Path kReroute{"machining", "jn_n", "packing", "jn_nw", "storage", "jn_sw", "receiving"};

TEST(AgentYieldTest, RerouteDuringBackoffTargetsEndOfNewPath) {
  auto backoff = BackoffStationAfterYield("backoff", kBackoff, "");
  ASSERT_EQ(RouteTarget("receiving", backoff), "park_2");

  backoff = BackoffStationAfterYield("reroute", kReroute, backoff);
  // 不一致时，车走到路径末站仍不算到达，接着就会读 path[path.size()]。
  EXPECT_EQ(RouteTarget("receiving", backoff), kReroute.back());
}

TEST(AgentYieldTest, RerouteWithoutUsablePathStillEndsBackoff) {
  // 指令里的路径用不了时，代理自己寻路，终点同样是本程目的地。
  EXPECT_EQ(RouteTarget("receiving", BackoffStationAfterYield("reroute", {}, "park_2")),
            "receiving");
}

TEST(AgentYieldTest, RerouteWithoutBackoffKeepsGoal) {
  EXPECT_EQ(RouteTarget("receiving", BackoffStationAfterYield("reroute", kReroute, "")),
            "receiving");
}

TEST(AgentYieldTest, LaterBackoffReplacesEarlierOne) {
  EXPECT_EQ(BackoffStationAfterYield("backoff", {"storage", "jn_sw", "park_1"}, "park_2"),
            "park_1");
}

TEST(AgentYieldTest, EmptyBackoffAndOtherActionsKeepBackoff) {
  EXPECT_EQ(BackoffStationAfterYield("backoff", {}, "park_2"), "park_2");
  EXPECT_EQ(BackoffStationAfterYield("pause", {}, "park_2"), "park_2");
}

TEST(AgentYieldTest, NextStationInsidePath) {
  EXPECT_EQ(NextStation(kReroute, 1), "jn_n");
  EXPECT_EQ(NextStation(kReroute, kReroute.size() - 1), "receiving");
}

TEST(AgentYieldTest, NextStationPastEndThrows) {
  EXPECT_THROW(NextStation(kReroute, kReroute.size()), std::out_of_range);
  const Path empty;
  EXPECT_THROW(NextStation(empty, 1), std::out_of_range);
}

}  // namespace
}  // namespace robot_simulation
