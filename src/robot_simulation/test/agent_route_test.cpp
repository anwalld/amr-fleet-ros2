#include "robot_simulation/agent_route.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

namespace robot_simulation {
namespace {

using Path = std::vector<std::string>;

// 与 robot_tasks/config/stations/workshop_2way_stations.yaml 的路网一致。
RouteGraph Workshop2Way() {
  RouteGraph graph;
  const auto one = [&graph](const std::string& from, const std::string& to, const double d) {
    graph[from].push_back({to, d});
  };
  const auto two = [&one](const std::string& a, const std::string& b, const double d) {
    one(a, b, d);
    one(b, a, d);
  };
  two("jn_sw", "receiving", 6.0);
  one("receiving", "jn_s", 6.0);
  one("jn_s", "jn_se", 6.0);
  one("jn_se", "assembly", 12.0);
  one("assembly", "jn_n", 6.0);
  two("jn_n", "packing", 6.0);
  one("packing", "jn_nw", 6.0);
  one("jn_nw", "storage", 6.0);
  one("storage", "jn_sw", 6.0);
  two("jn_s", "machining", 6.0);
  two("machining", "jn_n", 6.0);
  one("jn_sw", "park_1", 4.24);
  one("park_1", "receiving", 4.24);
  one("jn_s", "park_2", 4.24);
  one("park_2", "jn_se", 4.24);
  one("jn_se", "park_3", 6.71);
  one("park_3", "assembly", 6.71);
  one("assembly", "park_4", 4.24);
  one("park_4", "jn_n", 4.24);
  one("packing", "park_5", 4.24);
  one("park_5", "jn_nw", 4.24);
  one("jn_nw", "park_6", 4.24);
  one("park_6", "storage", 4.24);
  one("jn_se", "machining", 8.49);
  return graph;
}

class AgentRouteTest : public ::testing::Test {
 protected:
  RoutePlan Plan(const std::string& from, const std::string& to) {
    return PlanRoute(graph_, from, to, blocked_, avoid_edges_, avoid_nodes_);
  }

  const RouteGraph graph_ = Workshop2Way();
  std::set<EdgeKey> blocked_;
  std::set<EdgeKey> avoid_edges_;
  std::set<std::string> avoid_nodes_;
};

TEST_F(AgentRouteTest, RerouteThenBackoffCanStillReachGoal) {
  // 在 machining 收到避开 jn_s 的改道去 packing。
  avoid_nodes_ = {"jn_s"};
  ASSERT_EQ(Plan("machining", "packing").path, (Path{"machining", "jn_n", "packing"}));
  // 走到 jn_n 被退避到 park_5，绕行集合还在；停完从 park_5 回去只能经过 jn_s。
  ASSERT_EQ(Plan("jn_n", "park_5").path, (Path{"jn_n", "packing", "park_5"}));

  const auto plan = Plan("park_5", "packing");
  EXPECT_EQ(plan.path, (Path{"park_5", "jn_nw", "storage", "jn_sw", "receiving", "jn_s",
                             "machining", "jn_n", "packing"}));
  EXPECT_TRUE(plan.detours_dropped);
  EXPECT_TRUE(avoid_nodes_.empty());
}

TEST_F(AgentRouteTest, NightlyPark1ToStorageIsNotStuck) {
  // 夜跑 2026-11-18 4 台 rolling：robot_1 停在 park_1，报 13 次无路直到结束。
  avoid_nodes_ = {"jn_s"};
  const auto plan = Plan("park_1", "storage");
  EXPECT_EQ(plan.path, (Path{"park_1", "receiving", "jn_s", "machining", "jn_n", "packing",
                             "jn_nw", "storage"}));
}

TEST_F(AgentRouteTest, AvoidedEdgeIsDroppedWhenItIsTheOnlyWay) {
  avoid_edges_ = {MakeEdgeKey("receiving", "jn_s")};
  const auto plan = Plan("park_1", "jn_s");
  EXPECT_EQ(plan.path, (Path{"park_1", "receiving", "jn_s"}));
  EXPECT_TRUE(avoid_edges_.empty());
}

TEST_F(AgentRouteTest, DroppingDetoursKeepsBackoffDestination) {
  // 退避途中重新寻路：清掉绕行后仍去避让点，不改成本程目的地。
  avoid_nodes_ = {"jn_s"};
  EXPECT_EQ(Plan("machining", "park_2").path, (Path{"machining", "jn_s", "park_2"}));
}

TEST_F(AgentRouteTest, FeasibleDetourIsKept) {
  avoid_nodes_ = {"machining"};
  avoid_edges_ = {MakeEdgeKey("machining", "jn_n")};
  const auto plan = Plan("jn_s", "jn_n");
  EXPECT_EQ(plan.path, (Path{"jn_s", "jn_se", "assembly", "jn_n"}));
  EXPECT_FALSE(plan.detours_dropped);
  EXPECT_EQ(avoid_nodes_.size(), 1U);
  EXPECT_EQ(avoid_edges_.size(), 1U);
}

TEST_F(AgentRouteTest, RouteOnlyThroughBlockStaysEmpty) {
  blocked_ = {MakeEdgeKey("receiving", "jn_s")};
  const auto plan = Plan("park_5", "packing");
  EXPECT_TRUE(plan.path.empty());
  EXPECT_FALSE(plan.detours_dropped);
}

TEST_F(AgentRouteTest, DroppingDetoursNeverOpensBlock) {
  blocked_ = {MakeEdgeKey("receiving", "jn_s")};
  avoid_nodes_ = {"machining"};
  EXPECT_TRUE(Plan("park_5", "packing").path.empty());
  EXPECT_EQ(blocked_.size(), 1U);
}

TEST_F(AgentRouteTest, AfterDroppingDetoursStillGoesAroundBlock) {
  blocked_ = {MakeEdgeKey("jn_s", "machining")};
  avoid_nodes_ = {"jn_s"};
  EXPECT_EQ(Plan("park_1", "machining").path,
            (Path{"park_1", "receiving", "jn_s", "jn_se", "machining"}));
}

}  // namespace
}  // namespace robot_simulation
