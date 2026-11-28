#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include <robot_interfaces_business/srv/get_operator_snapshot.hpp>
#include <robot_interfaces_business/srv/submit_order.hpp>
#include <robot_interfaces_fleet/srv/control_remote_fleet_task.hpp>
#include <robot_interfaces_fleet/srv/update_fleet_robot_state.hpp>
#include <robot_interfaces_mission/msg/traffic_state.hpp>

#include "robot_console/ros_bridge.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

#include "robot_console_core/main_window.hpp"

namespace robot_console {
namespace {
namespace business = robot_interfaces_business::srv;
namespace fleet = robot_interfaces_fleet::srv;
using namespace std::chrono_literals;

class RosOperationsTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    static int argc = 1;
    static char name[] = "test_ros_operations";
    static char* argv[] = {name, nullptr};
    app_ = std::make_unique<QApplication>(argc, argv);
    rclcpp::InitOptions options;
    options.set_domain_id(217);
    rclcpp::init(0, nullptr, options);
    registerMetaTypes();
  }

  static void TearDownTestSuite() {
    rclcpp::shutdown();
    app_.reset();
  }

  void SetUp() override { executor_.add_node(server_); }

  bool waitFor(const std::function<bool()>& predicate, int timeout_ms = 6000) {
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout_ms) {
      executor_.spin_some();
      QTest::qWait(10);
    }
    return predicate();
  }

  static std::unique_ptr<QApplication> app_;
  rclcpp::Node::SharedPtr server_ = std::make_shared<rclcpp::Node>("test_console_ops_server");
  rclcpp::executors::SingleThreadedExecutor executor_;
  RosBridge bridge_;
};

std::unique_ptr<QApplication> RosOperationsTest::app_;

TEST_F(RosOperationsTest, IdleWindowOnlyCallsTheThreeCoreReadServices) {
  using Snapshot = business::GetOperatorSnapshot;
  using Stations = robot_interfaces_mission::srv::ListStations;
  using Routes = robot_interfaces_mission::srv::ListStationRoutes;
  using Update = fleet::UpdateFleetRobotState;

  int snapshots = 0;
  int stations = 0;
  int routes = 0;
  int writes = 0;
  auto snapshot = server_->create_service<Snapshot>(
      "/v2/get_operator_snapshot", [&](Snapshot::Request::SharedPtr, Snapshot::Response::SharedPtr response) {
        ++snapshots;
        response->success = true;
      });
  auto station = server_->create_service<Stations>(
      "/v2/list_stations", [&](Stations::Request::SharedPtr, Stations::Response::SharedPtr response) {
        ++stations;
        response->success = true;
      });
  auto route = server_->create_service<Routes>(
      "/v2/list_station_routes", [&](Routes::Request::SharedPtr, Routes::Response::SharedPtr response) {
        ++routes;
        response->success = true;
      });
  auto update = server_->create_service<Update>(
      "/v2/update_fleet_robot_state", [&](Update::Request::SharedPtr, Update::Response::SharedPtr) { ++writes; });
  MainWindow window(&bridge_, {}, nullptr, bridge_.operations());
  bridge_.start();
  ASSERT_TRUE(waitFor([&] {
    return snapshots >= 2 && stations >= 1 && routes >= 1 &&
           bridge_.operations()->available(Operation::SetRobotEnabled);
  }, 8000));
  EXPECT_EQ(writes, 0);
}

TEST_F(RosOperationsTest, RemoteControlPreservesRequestAndReceiptIdentity) {
  using Remote = fleet::ControlRemoteFleetTask;
  Remote::Request::SharedPtr captured;
  auto service = server_->create_service<Remote>(
      "/v2/control_remote_fleet_task", [&](Remote::Request::SharedPtr request, Remote::Response::SharedPtr response) {
        captured = request;
        response->success = true;
        response->task_id = request->task_id;
        response->execution_id = request->execution_id;
        response->revision = request->revision;
        response->latest_revision = request->revision;
        response->result = "PAUSING";
      });
  OperationResult response;
  QObject receiver;
  QObject::connect(bridge_.operations(), &OperationSource::operationCompleted, &receiver,
      [&](const OperationResult& result) { response = result; }, Qt::QueuedConnection);
  bridge_.start();
  ASSERT_TRUE(waitFor([&] { return bridge_.operations()->available(Operation::RemoteTaskControl); }));
  OperationRequest request;
  request.id = 72;
  request.operation = Operation::RemoteTaskControl;
  request.remote_action = RemoteTaskAction::Pause;
  request.target = "remote_job";
  request.execution_id = 234;
  request.revision = 11;
  ASSERT_TRUE(bridge_.operations()->submit(request));
  ASSERT_TRUE(waitFor([&] { return response.id == request.id; }));
  ASSERT_NE(captured, nullptr);
  EXPECT_EQ(captured->operation, Remote::Request::PAUSE);
  EXPECT_EQ(captured->task_id, "remote_job");
  EXPECT_EQ(captured->execution_id, 234u);
  EXPECT_EQ(captured->revision, 11u);
  EXPECT_EQ(response.remote_state, QStringLiteral("PAUSING"));
  EXPECT_EQ(response.remote_task_id, request.target);
  EXPECT_EQ(response.execution_id, request.execution_id);
  EXPECT_EQ(response.revision, request.revision);
}

TEST_F(RosOperationsTest, WorkflowRequestUsesTypedFieldsAndJson) {
  business::SubmitOrder::Request::SharedPtr captured;
  auto submit = server_->create_service<business::SubmitOrder>(
      "/v2/submit_order", [&captured](business::SubmitOrder::Request::SharedPtr request,
                                      business::SubmitOrder::Response::SharedPtr response) {
        captured = request;
        response->accepted = true;
        response->mission_ids = {"child_1", "child_2"};
      });
  OperationResult response;
  QObject receiver;
  QObject::connect(bridge_.operations(), &OperationSource::operationCompleted, &receiver,
      [&](const OperationResult& current) { response = current; }, Qt::QueuedConnection);
  bridge_.start();
  ASSERT_TRUE(waitFor([&] { return bridge_.operations()->available(Operation::SubmitWorkflow); }));
  OperationRequest request;
  request.id = 41;
  request.operation = Operation::SubmitWorkflow;
  request.target = "order_1";
  request.scenario_id = "warehouse";
  request.workflow_id = "outbound";
  request.priority = 12;
  ASSERT_TRUE(bridge_.operations()->submit(request));
  ASSERT_TRUE(waitFor([&] { return response.id == 41; }));
  ASSERT_NE(captured, nullptr);
  EXPECT_EQ(captured->order_id, "order_1");
  EXPECT_EQ(captured->order_type, "scenario_workflow");
  EXPECT_EQ(captured->priority, 12);
  const auto payload = QJsonDocument::fromJson(QByteArray::fromStdString(captured->payload_json)).object();
  EXPECT_EQ(payload.value("scenario_id").toString(), QStringLiteral("warehouse"));
  EXPECT_EQ(payload.value("workflow_id").toString(), QStringLiteral("outbound"));
  EXPECT_TRUE(payload.value("start_if_idle").toBool());
  EXPECT_FALSE(payload.value("preempt_current").toBool(true));
  EXPECT_EQ(response.outcome, OperationOutcome::Accepted);
  EXPECT_EQ(response.mission_ids, (QStringList{"child_1", "child_2"}));
}

TEST_F(RosOperationsTest, RealWindowTimesOutWithoutResendingAndReceivesSnapshot) {
  auto snapshot = server_->create_service<business::GetOperatorSnapshot>(
      "/v2/get_operator_snapshot", [](business::GetOperatorSnapshot::Request::SharedPtr,
                                      business::GetOperatorSnapshot::Response::SharedPtr response) {
        response->success = true;
        response->fleet_robot_ids = {"robot_2"};
        response->fleet_enabled = {false};
      });
  // 延迟回执夹具只接收写请求，快照服务由同一执行器继续应答。
  fleet::UpdateFleetRobotState::Request::SharedPtr captured;
  int requests = 0;
  auto enabled = server_->create_service<fleet::UpdateFleetRobotState>(
      "/v2/update_fleet_robot_state",
      [&](std::shared_ptr<rmw_request_id_t>, fleet::UpdateFleetRobotState::Request::SharedPtr request) {
        ++requests;
        captured = request;
      });
  MainWindow window(&bridge_, {}, nullptr, bridge_.operations());
  window.show();
  auto* robot = window.findChild<QComboBox*>("robotTarget");
  auto* toggle = window.findChild<QCheckBox*>("robotEnabled");
  auto* send = window.findChild<QPushButton*>("setRobotEnabled");
  auto* history = window.findChild<QTableWidget*>("operationHistory");
  ASSERT_TRUE(robot && toggle && send && history);

  bridge_.start();
  ASSERT_TRUE(waitFor([&] { return robot->count() == 1; }));
  robot->setCurrentIndex(0);
  toggle->setChecked(false);
  ASSERT_TRUE(waitFor([&] { return send->isEnabled(); }));
  QElapsedTimer clock;
  clock.start();
  send->click();
  send->click();
  ASSERT_TRUE(waitFor([&] { return requests == 1; }));
  ASSERT_NE(captured, nullptr);
  EXPECT_EQ(captured->robot_id, "robot_2");
  EXPECT_FALSE(captured->enabled);
  EXPECT_TRUE(captured->update_enabled);
  EXPECT_FALSE(captured->update_state);
  EXPECT_FALSE(captured->update_battery_voltage);
  EXPECT_FALSE(captured->update_current_station_id);
  EXPECT_TRUE(waitFor([&] { return history->item(0, 3)->text().contains("已停用"); }));
  EXPECT_TRUE(waitFor([&] { return history->item(0, 2)->text().contains("结果未知"); }));
  EXPECT_GE(clock.elapsed(), 3000);
  EXPECT_EQ(requests, 1);
  ASSERT_TRUE(send->isEnabled());
  send->click();
  EXPECT_TRUE(waitFor([&] { return requests == 2; }));
  window.close();
  bridge_.stop();
}

}  // namespace
}  // namespace robot_console
