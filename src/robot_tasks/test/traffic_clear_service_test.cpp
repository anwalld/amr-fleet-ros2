#include <gtest/gtest.h>

#include <chrono>

#include "robot_tasks/traffic_service.hpp"

namespace robot_tasks {
namespace {
using namespace std::chrono_literals;
namespace srv = robot_interfaces_mission::srv;

class TrafficClearServiceTest : public testing::Test {
 protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }

  template<class Service>
  std::shared_ptr<typename Service::Response> call(
      typename rclcpp::Client<Service>::SharedPtr client, typename Service::Request request) {
    auto future = client->async_send_request(std::make_shared<typename Service::Request>(request));
    if (executor_.spin_until_future_complete(future, 2s) != rclcpp::FutureReturnCode::SUCCESS) {
      return nullptr;
    }
    return future.get();
  }

  MissionContext context_;
  rclcpp::Node::SharedPtr server_ = std::make_shared<rclcpp::Node>("test_traffic_clear_server");
  rclcpp::Node::SharedPtr client_ = std::make_shared<rclcpp::Node>("test_traffic_clear_client");
  TrafficService traffic_{server_.get(), context_};
  rclcpp::executors::SingleThreadedExecutor executor_;

  void SetUp() override {
    executor_.add_node(server_);
    executor_.add_node(client_);
  }
};

TEST_F(TrafficClearServiceTest, ChecksPreviewBeforeReleasingAndGrantsAfterMatch) {
  auto preview = client_->create_client<srv::PreviewTrafficReservationClear>(
      "/v2/preview_traffic_reservation_clear");
  auto clear = client_->create_client<srv::ClearTrafficReservationIfMatches>(
      "/v2/clear_traffic_reservation_if_matches");
  ASSERT_TRUE(preview->wait_for_service(3s));
  ASSERT_TRUE(clear->wait_for_service(3s));

  const std::string edge = "route_edge:a__b";
  const std::string node = "route_node:b";
  ASSERT_TRUE(traffic_.ReserveRouteLocksForMission("order_a", "robot_1", {edge}, nullptr));
  ASSERT_TRUE(traffic_.ReserveRouteLocksForMission("order_b", "robot_1", {edge, node}, nullptr));
  context_.route_wait_queues[edge].push_back(RouteWaiter{"robot_2", "a", "b", 1, 1, {edge}});
  int grants = 0;
  traffic_.SetGrantCallback([&](const std::string& event, const RouteGrant& grant) {
    if (event == "granted" && grant.robot_id == "robot_2") ++grants;
  });

  srv::PreviewTrafficReservationClear::Request preview_request;
  preview_request.resource_id = edge;
  const auto impact = call<srv::PreviewTrafficReservationClear>(preview, preview_request);
  ASSERT_NE(impact, nullptr);
  ASSERT_TRUE(impact->found);
  EXPECT_EQ(impact->owner_id, "robot_1");
  EXPECT_EQ(impact->resource_ids, (std::vector<std::string>{edge, node}));
  EXPECT_TRUE(impact->resume_robot_ids.empty());

  srv::ClearTrafficReservationIfMatches::Request request;
  request.resource_id = edge;
  request.expected_owner_id = impact->owner_id;
  request.expected_resource_ids = {edge};
  auto response = call<srv::ClearTrafficReservationIfMatches>(clear, request);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->result, srv::ClearTrafficReservationIfMatches::Response::CONFLICT);
  EXPECT_EQ(context_.route_locks.size(), 2U);
  EXPECT_EQ(context_.route_locks.at(edge), "robot_1");
  EXPECT_EQ(context_.route_wait_queues.count(edge), 1U);
  EXPECT_EQ(grants, 0);

  request.expected_resource_ids = {node, edge};
  response = call<srv::ClearTrafficReservationIfMatches>(clear, request);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->result, srv::ClearTrafficReservationIfMatches::Response::SUCCESS);
  EXPECT_EQ(grants, 1);
  EXPECT_EQ(context_.route_locks.at(edge), "robot_2");
  EXPECT_EQ(context_.route_locks.count(node), 0U);
  EXPECT_TRUE(context_.route_locks_by_mission.empty());

  request.expected_owner_id.clear();
  response = call<srv::ClearTrafficReservationIfMatches>(clear, request);
  ASSERT_NE(response, nullptr);
  EXPECT_EQ(response->result, srv::ClearTrafficReservationIfMatches::Response::ERROR);
}

TEST_F(TrafficClearServiceTest, MatchingPreviewUnfreezesTheWholePausedGroup) {
  auto preview = client_->create_client<srv::PreviewTrafficReservationClear>(
      "/v2/preview_traffic_reservation_clear");
  auto clear = client_->create_client<srv::ClearTrafficReservationIfMatches>(
      "/v2/clear_traffic_reservation_if_matches");
  ASSERT_TRUE(preview->wait_for_service(3s));
  ASSERT_TRUE(clear->wait_for_service(3s));

  context_.station_file = TRAFFIC_CLEAR_TEST_STATIONS;
  ASSERT_TRUE(traffic_.RequestRouteSegment("robot_1", "", "a", 1).granted);
  ASSERT_TRUE(traffic_.RequestRouteSegment("robot_2", "", "b", 1).granted);
  ASSERT_TRUE(traffic_.RequestRouteSegment("robot_1", "a", "b", 1).newly_queued);
  ASSERT_TRUE(traffic_.RequestRouteSegment("robot_2", "b", "a", 1).newly_queued);
  auto is_paused = [](const auto& decision) {
    return decision.message.find("paused by deadlock fallback") != std::string::npos;
  };
  ASSERT_TRUE(is_paused(traffic_.RequestRouteSegment("robot_1", "", "a", 1)));
  ASSERT_TRUE(is_paused(traffic_.RequestRouteSegment("robot_2", "", "b", 1)));

  srv::PreviewTrafficReservationClear::Request query;
  query.resource_id = "route_node:a";
  const auto impact = call<srv::PreviewTrafficReservationClear>(preview, query);
  ASSERT_NE(impact, nullptr);
  ASSERT_TRUE(impact->found);
  EXPECT_EQ(impact->resume_robot_ids, (std::vector<std::string>{"robot_1", "robot_2"}));

  srv::ClearTrafficReservationIfMatches::Request request;
  request.resource_id = query.resource_id;
  request.expected_owner_id = impact->owner_id;
  request.expected_resource_ids = impact->resource_ids;
  request.expected_resume_robot_ids = {"robot_1"};
  const auto conflict = call<srv::ClearTrafficReservationIfMatches>(clear, request);
  ASSERT_NE(conflict, nullptr);
  EXPECT_EQ(conflict->result, srv::ClearTrafficReservationIfMatches::Response::CONFLICT);
  EXPECT_TRUE(is_paused(traffic_.RequestRouteSegment("robot_1", "", "a", 1)));
  EXPECT_TRUE(is_paused(traffic_.RequestRouteSegment("robot_2", "", "b", 1)));
  EXPECT_EQ(context_.route_locks.at(query.resource_id), "robot_1");

  request.expected_resume_robot_ids = {"robot_2", "robot_1"};
  const auto accepted = call<srv::ClearTrafficReservationIfMatches>(clear, request);
  ASSERT_NE(accepted, nullptr);
  EXPECT_EQ(accepted->result, srv::ClearTrafficReservationIfMatches::Response::SUCCESS);
  EXPECT_FALSE(is_paused(traffic_.RequestRouteSegment("robot_1", "", "a", 1)));
  EXPECT_FALSE(is_paused(traffic_.RequestRouteSegment("robot_2", "", "b", 1)));
  EXPECT_EQ(context_.route_locks.at(query.resource_id), "robot_2");
}

TEST_F(TrafficClearServiceTest, LegacyClearStillReleasesOldTaskNumber) {
  auto clear = client_->create_client<srv::ClearTrafficReservation>("/v2/clear_traffic_reservation");
  ASSERT_TRUE(clear->wait_for_service(3s));
  const std::string edge = "route_edge:a__b";
  const std::string node = "route_node:b";
  ASSERT_TRUE(traffic_.ReserveRouteLocksForMission("old_task", "", {edge, node}, nullptr));
  srv::ClearTrafficReservation::Request request;
  request.resource_id = edge;
  const auto response = call<srv::ClearTrafficReservation>(clear, request);
  ASSERT_NE(response, nullptr);
  EXPECT_TRUE(response->success);
  EXPECT_EQ(response->owner_id, "old_task");
  EXPECT_TRUE(context_.route_locks.empty());
}
}  // namespace
}  // namespace robot_tasks
