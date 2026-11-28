// 用真实窗口和 RosBridge 执行 Ops 验收。每项使用运行器指定的独立 ROS 域。
#include "robot_console/ros_bridge.hpp"

#include <robot_interfaces_business/srv/get_workflow_status.hpp>
#include <robot_interfaces/msg/robot_state.hpp>
#include <robot_interfaces_fleet/srv/submit_fleet_station_task.hpp>
#include <robot_interfaces_fleet/srv/update_fleet_robot_state.hpp>
#include <robot_interfaces_mission/srv/block_station_route.hpp>
#include <robot_interfaces_mission/srv/release_route_segment.hpp>
#include <robot_interfaces_mission/srv/request_route_segment.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QGraphicsLineItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/map_scene.hpp"

using namespace robot_console;
namespace business = robot_interfaces_business::srv;
namespace fleet = robot_interfaces_fleet::srv;
namespace mission = robot_interfaces_mission::srv;
using namespace std::chrono_literals;

namespace {
const QString kEdge = QStringLiteral("route_edge:receiving__storage_a");
const QString kOtherEdge = QStringLiteral("route_edge:packing__storage_a");

void record(const char* step, QJsonObject fields) {
  fields.insert(QStringLiteral("step"), QString::fromLatin1(step));
  qInfo().noquote() << "OPS_EVIDENCE" << QJsonDocument(fields).toJson(QJsonDocument::Compact);
}

template<class Predicate>
bool until(Predicate predicate, int timeout = 8000) {
  QElapsedTimer clock;
  clock.start();
  while (!predicate() && clock.elapsed() < timeout) QTest::qWait(20);
  return predicate();
}
}  // namespace

class ConsoleOpsAcceptance : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { registerMetaTypes(); }

  void init() {
    fixture_ = std::make_shared<rclcpp::Node>("console_acceptance_fixture");
    executor_.add_node(fixture_);
    pump_.setInterval(15);
    connect(&pump_, &QTimer::timeout, this, [this] { executor_.spin_some(); });
    pump_.start();
    bridge_ = std::make_unique<RosBridge>();
    window_ = std::make_unique<MainWindow>(bridge_.get(), MonotonicMsFn{}, nullptr, bridge_->operations());
    results_.clear();
    connect(bridge_->operations(), &OperationSource::operationCompleted, this,
            [this](const OperationResult& result) { results_.append(result); }, Qt::QueuedConnection);
    connect(bridge_.get(), &DataSource::snapshotReceived, this,
            [this](const auto& snapshot) { snapshot_ = snapshot; });
    connect(bridge_.get(), &DataSource::trafficReceived, this,
            [this](const auto& traffic) { traffic_ = traffic; });
    connect(bridge_.get(), &DataSource::poseReceived, this, [this](const PoseSample& pose) {
      poses_.insert(pose.robot, pose);
    });
    window_->show();
    bridge_->start();
  }

  void cleanup() {
    pump_.stop();
    pump_.disconnect(this);
    window_.reset();
    bridge_.reset();
    executor_.remove_node(fixture_);
    fixture_.reset();
  }

  void workflow_submit() {
    const auto submission = submitWorkflow(false);
    QVERIFY(!submission.first.isEmpty());
    QCOMPARE(submission.second.outcome, OperationOutcome::Accepted);
    QVERIFY(submission.second.mission_ids.size() >= 2);
    QVERIFY(until([&] { return field<QLabel>("workflowStatus")->text().contains("QUEUED"); }));
    const auto status = queryWorkflow(submission.first);
    QVERIFY(status && status->success);
    QCOMPARE(QString::fromStdString(status->order_id), submission.first);
    for (const auto& id : submission.second.mission_ids) {
      QVERIFY(std::find(status->queued_mission_ids.begin(), status->queued_mission_ids.end(),
                        id.toStdString()) != status->queued_mission_ids.end());
    }
    record("workflow_observed", {{"target", submission.first}, {"state", "QUEUED"},
          {"children", QJsonArray::fromStringList(submission.second.mission_ids)}});
  }

  void workflow_control() {
    const auto submission = submitWorkflow(false);
    const auto& order = submission.first;
    QVERIFY(!order.isEmpty());
    QVERIFY(until([&] { return field<QPushButton>("pauseWorkflow")->isEnabled(); }));
    auto* include_active = field<QCheckBox>("includeActive");
    include_active->setChecked(true);
    QVERIFY(trigger("pauseWorkflow"));
    QVERIFY(until([&] { return field<QLabel>("workflowStatus")->text().contains("PAUSED"); }));
    const auto paused = queryWorkflow(order);
    QVERIFY(paused && paused->success && paused->paused);
    QVERIFY(trigger("resumeWorkflow"));
    QVERIFY(until([&] {
      return snapshot_.mission_active && submission.second.mission_ids.contains(snapshot_.active_mission_id)
             && !snapshot_.paused_workflow_order_ids.contains(order);
    }));
    const auto resumed = queryWorkflow(order);
    QVERIFY(resumed && resumed->success && !resumed->paused);
    const QString active = QString::fromStdString(resumed->running_mission_id);
    QVERIFY(!active.isEmpty());
    QVERIFY(!resumed->queued_mission_ids.empty());
    QVERIFY(until([&] { return field<QPushButton>("cancelWorkflow")->isEnabled(); }));
    const int result_index = results_.size();
    scheduleModalConfirmation(QMessageBox::Yes);
    QVERIFY(trigger("cancelWorkflow"));
    const auto canceled = reply(result_index);
    QVERIFY(canceled);
    QCOMPARE(canceled->outcome, OperationOutcome::Accepted);
    QVERIFY(canceled->active_requested);
    QVERIFY(canceled->mission_ids.contains(active));
    for (const auto& id : resumed->queued_mission_ids) {
      QVERIFY(canceled->mission_ids.contains(QString::fromStdString(id)));
    }
    QVERIFY(until([&] {
      for (const auto& id : canceled->mission_ids) {
        if (snapshot_.queued_mission_ids.contains(id)) return false;
      }
      const int i = snapshot_.recent_event_mission_ids.indexOf(active);
      return i >= 0 && snapshot_.recent_event_states.value(i) == QStringLiteral("CANCELED");
    }, 15000));
    const auto status = queryWorkflow(order);
    QVERIFY(status && status->queued_mission_ids.empty());
    record("workflow_control_observed", {{"target", order}, {"paused_first", paused->paused},
          {"paused_after_resume", resumed->paused}, {"final_state", QString::fromStdString(status->state)},
          {"active", active}, {"active_terminal", "CANCELED"},
          {"canceled", QJsonArray::fromStringList(canceled->mission_ids)}});
  }

  void fleet_submit_remote() {
    QVERIFY(until([&] { return snapshot_.fleet_robot_ids.contains("robot_2"); }, 30000));
    QVERIFY(setFleetEnabled("robot_2", false));
    const auto submission = submitTransport("receiving", "storage");
    QVERIFY(!submission.first.isEmpty());
    QCOMPARE(submission.second.outcome, OperationOutcome::Accepted);
    QCOMPARE(submission.second.assigned_robot_id, QStringLiteral("robot_1"));
    QVERIFY(until([&] {
      const int i = snapshot_.remote_task_ids.indexOf(submission.first);
      return i >= 0 && snapshot_.remote_task_robot_ids.value(i) == QStringLiteral("robot_1") &&
             snapshot_.remote_task_states.value(i) == QStringLiteral("FINISHED") &&
             historyObservation(submission.first).contains("FINISHED");
    }, 120000));
    record("remote_task_observed", {{"target", submission.first}, {"robot", "robot_1"}, {"state", "FINISHED"}});
  }

  void fleet_submit_local() {
    QVERIFY(until([&] { return snapshot_.fleet_robot_ids.contains("robot_2"); }, 30000));
    QVERIFY(setFleetEnabled("robot_2", false));
    const auto submission = submitTransport("receiving", "storage_a");
    QVERIFY(!submission.first.isEmpty());
    QCOMPARE(submission.second.outcome, OperationOutcome::Accepted);
    QCOMPARE(submission.second.assigned_robot_id, QStringLiteral("robot_1"));
    const auto mission_id = submission.second.mission_id;
    QVERIFY(!mission_id.isEmpty());
    QVERIFY(until([&] {
      const int i = snapshot_.recent_event_mission_ids.indexOf(mission_id);
      return i >= 0 && snapshot_.recent_event_states.value(i) == QStringLiteral("FINISHED") &&
             historyObservation(submission.first).contains("FINISHED");
    }, 120000));
    record("local_task_observed", {{"target", submission.first}, {"robot", "robot_1"},
          {"mission_id", mission_id}, {"state", "FINISHED"}});
  }

  void route_block() {
    QVERIFY(until([&] { return window_->mapScene()->edge("receiving", "storage_a") != nullptr; }, 30000));
    auto blocked = [&] {
      return window_->mapScene()->edge("receiving", "storage_a")->line->pen().color() == MapScene::blockColor();
    };
    QElapsedTimer elapsed;
    elapsed.start();
    const auto external = fixtureCall<mission::BlockStationRoute>("/v2/block_station_route",
        [](auto& request) {
          request.from_station = "receiving";
          request.to_station = "storage_a";
          request.reason = "ops_external";
        });
    QVERIFY(external && external->success);
    QVERIFY(until(blocked, std::max(1, 5000 - static_cast<int>(elapsed.elapsed()))));
    const auto external_ms = elapsed.elapsed();
    QVERIFY(external_ms <= 5000);
    select("routeTarget", kEdge);
    auto index = results_.size();
    QVERIFY(trigger("unblockRoute"));
    QVERIFY(accepted(index));
    QVERIFY(until([&] { return !blocked(); }));
    index = results_.size();
    QVERIFY(trigger("blockRoute"));
    QVERIFY(accepted(index));
    QVERIFY(until(blocked));
    index = results_.size();
    QVERIFY(trigger("unblockRoute"));
    QVERIFY(accepted(index));
    QVERIFY(until([&] { return !blocked(); }));
    QVERIFY(reserve("alpha", "receiving", "storage_a", true));
    index = results_.size();
    QVERIFY(trigger("blockRoute"));
    const auto rejected = reply(index);
    QVERIFY(rejected && rejected->outcome == OperationOutcome::Rejected);
    QVERIFY(!rejected->message.isEmpty());
    QVERIFY(until([&] { return owner(kEdge) == QStringLiteral("alpha"); }));
    QVERIFY(!blocked());
    record("route_observed", {{"target", kEdge}, {"external_refresh_ms", external_ms},
          {"occupied_owner", owner(kEdge)}, {"rejection", rejected->message}});
  }

  void fleet_enabled() {
    QVERIFY(until([&] { return snapshot_.fleet_robot_ids.contains("robot_2"); }, 30000));
    select("robotTarget", "robot_2");
    const int original_index = snapshot_.fleet_robot_ids.indexOf("robot_2");
    const auto state = snapshot_.fleet_states.value(original_index);
    const auto voltage = snapshot_.fleet_battery_voltage.value(original_index);
    for (bool enabled : {false, true}) {
      field<QCheckBox>("robotEnabled")->setChecked(enabled);
      const auto index = results_.size();
      QVERIFY(trigger("setRobotEnabled"));
      QVERIFY(accepted(index));
      QVERIFY(until([&] {
        const int i = snapshot_.fleet_robot_ids.indexOf("robot_2");
        return i >= 0 && i < snapshot_.fleet_enabled.size() && snapshot_.fleet_enabled[i] == enabled &&
               historyObservation("robot_2").contains(enabled ? "已启用" : "已停用");
      }));
    }
    const int i = snapshot_.fleet_robot_ids.indexOf("robot_2");
    QCOMPARE(snapshot_.fleet_states.value(i), state);
    QCOMPARE(snapshot_.fleet_battery_voltage.value(i), voltage);
    record("fleet_flags_observed", {{"target", "robot_2"}, {"sequence", QJsonArray{false, true}},
          {"state", state}, {"voltage", voltage}});
  }

  void clear_match() {
    QVERIFY(pauseGroup());
    const auto preview = previewClear(kEdge);
    QVERIFY(preview && preview->preview.found);
    QCOMPARE(preview->preview.owner_id, QStringLiteral("alpha"));
    QCOMPARE(preview->preview.resume_robot_ids, (QStringList{"alpha", "beta"}));
    const int i = results_.size();
    QVERIFY(trigger("confirmClear"));
    QVERIFY(accepted(i));
    QVERIFY(until([&] {
      for (const auto& id : preview->preview.resource_ids) {
        if (owner(id) == QStringLiteral("alpha")) return false;
      }
      return owner(kEdge) == QStringLiteral("beta");
    }));
    QVERIFY(reserve("beta", "storage_a", "receiving", true));
    record("clear_match_observed", {{"target", kEdge}, {"released", QJsonArray::fromStringList(preview->preview.resource_ids)},
          {"resume", QJsonArray::fromStringList(preview->preview.resume_robot_ids)}, {"new_owner", owner(kEdge)}});
  }

  void clear_owner_conflict() {
    QVERIFY(reserve("alpha", "receiving", "storage_a", true));
    const auto preview = previewClear(kEdge);
    QVERIFY(preview && preview->preview.owner_id == QStringLiteral("alpha"));
    QVERIFY(release("alpha"));
    QVERIFY(reserve("beta", "receiving", "storage_a", true));
    QVERIFY(checkConflict("beta"));
    record("clear_owner_conflict_observed", {{"target", kEdge}, {"previous", "alpha"}, {"current", owner(kEdge)}});
  }

  void clear_scope_conflict() {
    QVERIFY(reserve("alpha", "receiving", "storage_a", true));
    const auto preview = previewClear(kEdge);
    QVERIFY(preview && preview->preview.owner_id == QStringLiteral("alpha"));
    QVERIFY(reserve("alpha", "storage_a", "packing", true));
    QVERIFY(checkConflict("alpha"));
    QVERIFY(until([&] { return owner(kOtherEdge) == QStringLiteral("alpha"); }));
    const auto second_preview = results_.last();
    QCOMPARE(second_preview.preview.resource_ids.size(), 2);
    QVERIFY(release("alpha"));

    // 资源占用者和集合保持一致，仅改变兜底暂停组。
    QVERIFY(reserve("alpha", "receiving", "storage_a", true));
    const auto before_pause = previewClear(kEdge);
    QVERIFY(before_pause && before_pause->preview.resume_robot_ids.isEmpty());
    QVERIFY(pauseGroup(true));
    QVERIFY(checkConflict("alpha"));
    QCOMPARE(traffic_.last_resolution_action, QStringLiteral("pause"));
    const auto paused = segment("alpha", "storage_a", "packing");
    QVERIFY(paused && !paused->granted && paused->message.find("paused by deadlock fallback") != std::string::npos);
    record("clear_scope_conflict_observed", {{"target", kEdge}, {"owner", owner(kEdge)},
          {"resource_change", true}, {"resume_change", true}});
  }

  void alarm_sim_stop() {
    auto* status = field<QLabel>("fleetStopStatus");
    QVERIFY(until([&] { return status->text() == QStringLiteral("仿真停止已设置"); }, 15000));
    auto confirm = [this] { scheduleModalConfirmation(QMessageBox::Yes); };
    const auto confirm_no = [this] { scheduleModalConfirmation(QMessageBox::No); };
    confirm_no();
    QVERIFY(trigger("releaseFleetSim"));
    QCOMPARE(results_.size(), 0);
    confirm();
    QVERIFY(trigger("releaseFleetSim"));
    QVERIFY(accepted(0));
    QVERIFY(until([&] { return status->text() == QStringLiteral("仿真停止已解除"); }));
    QVERIFY(until([&] { return snapshot_.fleet_robot_ids.contains("robot_2") && poses_.contains("robot_1"); }, 30000));
    QVERIFY(setFleetEnabled("robot_2", false));
    // 发布前两条结果属于正式的释放请求和搬运请求。
    QVERIFY(readyStations("receiving", "storage"));
    QVERIFY(trigger("submitTransport"));
    const QString task = lastTarget();
    QVERIFY(accepted(1));
    QVERIFY(until([&] { return std::abs(poses_.value("robot_1").vx) > 0.1; }, 30000));
    QVERIFY(trigger("stopFleetSim"));
    QVERIFY(accepted(2));
    QVERIFY(until([&] {
      return status->text() == QStringLiteral("仿真停止已设置") &&
             std::abs(poses_.value("robot_1").vx) < 0.001 &&
             std::abs(poses_.value("robot_1").wz) < 0.001;
    }));
    const auto stopped = poses_.value("robot_1");
    QStringList reservations;
    for (int i = 0; i < traffic_.owner_ids.size(); ++i) {
      if (traffic_.owner_ids[i] == QStringLiteral("robot_1")) reservations.append(traffic_.resource_ids.value(i));
    }
    QVERIFY(!reservations.isEmpty());
    QTest::qWait(1200);
    const auto after = poses_.value("robot_1");
    QVERIFY(after.stamp > stopped.stamp);
    QVERIFY(std::hypot(after.x - stopped.x, after.y - stopped.y) < 0.001);
    for (const auto& resource : reservations) QCOMPARE(owner(resource), QStringLiteral("robot_1"));
    confirm();
    QVERIFY(trigger("releaseFleetSim"));
    QVERIFY(accepted(3));
    QVERIFY(until([&] { return std::abs(poses_.value("robot_1").vx) > 0.1; }));
    record("alarm_sim_stop", {{"task", task}, {"robot", "robot_1"}, {"held_resources", reservations.size()},
          {"stopped_ms", 1200}, {"resumed", true}});
  }

  void alarm_events_and_recovery() {
    using Snapshot = business::GetOperatorSnapshot;
    using Stations = mission::ListStations;
    using Routes = mission::ListStationRoutes;
    bool available = true;
    bool graph_available = true;
    int graph_requests = 0;
    auto snapshot = fixture_->create_service<Snapshot>("/v2/get_operator_snapshot",
        [&](Snapshot::Request::SharedPtr, Snapshot::Response::SharedPtr response) {
          response->success = available;
          response->message = available ? "" : "fixture unavailable";
        });
    auto stations = fixture_->create_service<Stations>("/v2/list_stations",
        [&](Stations::Request::SharedPtr, Stations::Response::SharedPtr response) {
          response->success = graph_available;
          response->station_ids = {"a", "b"};
          response->x = {0.0, 1.0};
          response->y = {0.0, 0.0};
          ++graph_requests;
        });
    auto routes = fixture_->create_service<Routes>("/v2/list_station_routes",
        [&](Routes::Request::SharedPtr, Routes::Response::SharedPtr response) {
          response->success = graph_available;
          response->from_station = {"a"};
          response->to_station = {"b"};
          response->enabled = {true};
        });
    auto publisher = fixture_->create_publisher<robot_interfaces::msg::RobotState>("/fault_supervisor/state", 10);
    auto connected = [&] { return window_->statusText().contains(QStringLiteral("已连接")); };
    auto have_map = [&] { return window_->mapScene()->edge("a", "b") != nullptr; };
    QVERIFY(until([&] { return connected() && have_map() && publisher->get_subscription_count() > 0; }));
    robot_interfaces::msg::RobotState alarm;
    alarm.state = "ERROR";
    alarm.message = "acceptance fixture";
    publisher->publish(alarm);
    auto* events = field<QListWidget>("consoleEvents");
    QVERIFY(until([&] { return events->count() == 1; }));
    publisher->publish(alarm);
    alarm.state = "OK";
    publisher->publish(alarm);
    QVERIFY(until([&] { return events->count() == 2; }));
    QVERIFY(events->item(0)->text().contains("OK"));
    const int before = graph_requests;
    available = false;
    graph_available = false;
    QVERIFY(until([&] { return !connected(); }));
    QVERIFY(have_map());
    available = true;
    QVERIFY(until([&] { return window_->statusText().contains(QStringLiteral("等待路网")); }));
    QVERIFY(!connected());
    graph_available = true;
    QVERIFY(until(connected));
    QVERIFY(graph_requests > before);
    QVERIFY(have_map());
    auto heartbeat = fixture_->create_publisher<std_msgs::msg::Bool>(
        "/fleet/sim_stop/state", rclcpp::QoS(1).reliable().transient_local());
    QVERIFY(until([&] { return heartbeat->get_subscription_count() > 0; }));
    std_msgs::msg::Bool stopped;
    stopped.data = true;
    heartbeat->publish(stopped);
    auto* state = field<QLabel>("fleetStopStatus");
    QVERIFY(until([&] { return state->text() == QStringLiteral("仿真停止已设置"); }));
    heartbeat.reset();
    QVERIFY(until([&] { return state->text() == QStringLiteral("仿真控制状态未知"); }, 3500));
    QVERIFY(!field<QPushButton>("releaseFleetSim")->isEnabled());
    record("alarm_events_and_recovery", {{"events", events->count()}, {"graph_requests", graph_requests - before},
          {"recovered", true}, {"map_retained", true}, {"stop_status_expired", true}});
  }

  void submit_timeout() {
    using Submit = fleet::SubmitFleetStationTask;
    using Snapshot = business::GetOperatorSnapshot;
    using Stations = mission::ListStations;
    using Routes = mission::ListStationRoutes;
    QString committed_task;
    int requests = 0;
    int snapshots = 0;
    auto submit = fixture_->create_service<Submit>("/v2/submit_fleet_station_task",
        [&](std::shared_ptr<rmw_request_id_t>, Submit::Request::SharedPtr request) {
          committed_task = QString::fromStdString(request->task_id);
          ++requests;
        });
    auto snapshot = fixture_->create_service<Snapshot>("/v2/get_operator_snapshot",
        [&](Snapshot::Request::SharedPtr, Snapshot::Response::SharedPtr response) {
          response->success = true;
          ++snapshots;
          if (!committed_task.isEmpty()) {
            response->remote_task_ids = {committed_task.toStdString()};
            response->remote_task_robot_ids = {"robot_2"};
            response->remote_task_states = {"RUNNING"};
          }
        });
    auto stations = fixture_->create_service<Stations>("/v2/list_stations",
        [](Stations::Request::SharedPtr, Stations::Response::SharedPtr response) {
          response->success = true;
          response->station_ids = {"receiving", "storage_a"};
          response->x = {0.0, 1.0};
          response->y = {0.0, 0.0};
        });
    auto routes = fixture_->create_service<Routes>("/v2/list_station_routes",
        [](Routes::Request::SharedPtr, Routes::Response::SharedPtr response) {
          response->success = true;
          response->from_station = {"receiving"};
          response->to_station = {"storage_a"};
          response->enabled = {true};
        });
    auto traffic = fixture_->create_publisher<robot_interfaces_mission::msg::TrafficState>("/traffic/state", 10);
    QVERIFY(readyStations("receiving", "storage_a"));
    auto* button = field<QPushButton>("submitTransport");
    QElapsedTimer elapsed;
    elapsed.start();
    const int snapshots_before = snapshots;
    QVERIFY(trigger("submitTransport"));
    button->click();
    QVERIFY(until([&] { return requests == 1 && traffic->get_subscription_count() > 0; }));
    robot_interfaces_mission::msg::TrafficState state;
    state.mode = "rolling";
    state.resource_ids = {kEdge.toStdString()};
    state.owner_ids = {"traffic_block:delay_fixture"};
    traffic->publish(state);
    QVERIFY(until([&] {
      return window_->mapScene()->edge("receiving", "storage_a")->line->pen().color() == MapScene::blockColor();
    }));
    const auto timeout = reply(0);
    QVERIFY(timeout && timeout->outcome == OperationOutcome::Unknown);
    QVERIFY(elapsed.elapsed() >= 3000 && elapsed.elapsed() < 4500);
    QVERIFY(until([&] { return historyObservation(committed_task).contains("RUNNING"); }));
    QCOMPARE(requests, 1);
    QVERIFY(snapshots > snapshots_before + 1);
    record("timeout_observed", {{"target", committed_task}, {"requests", requests},
          {"elapsed_ms", elapsed.elapsed()}, {"snapshots", snapshots - snapshots_before}, {"map_refreshed", true}});
  }

 private:
  bool readyStations(const QString& pickup, const QString& dropoff) {
    if (!until([&] { return field<QComboBox>("pickupStation")->findText(pickup) >= 0 &&
                            field<QComboBox>("dropoffStation")->findText(dropoff) >= 0; }, 30000)) return false;
    select("pickupStation", pickup);
    select("dropoffStation", dropoff);
    return true;
  }

  template<class T>
  T* field(const char* name) const {
    return window_->findChild<T*>(QString::fromLatin1(name));
  }

  bool trigger(const char* name) {
    auto* button = field<QPushButton>(name);
    if (!button || !until([&] { return button->isEnabled(); })) return false;
    button->click();
    return true;
  }

  void scheduleModalConfirmation(const QMessageBox::StandardButton button,
                                 const int timeout_ms = 15000) {
    auto* timer = new QTimer(window_.get());
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, window_.get(), [timer, elapsed, button, timeout_ms]() {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        if (auto* target = box->button(button)) {
          timer->stop();
          timer->deleteLater();
          target->click();
          return;
        }
      }
      if (elapsed->elapsed() >= timeout_ms) {
        timer->stop();
        timer->deleteLater();
      }
    });
    timer->start();
  }

  void select(const char* name, const QString& value) {
    field<QComboBox>(name)->setCurrentText(value);
  }

  QString historyObservation(const QString& target) const {
    auto* history = field<QTableWidget>("operationHistory");
    for (int i = history->rowCount() - 1; i >= 0; --i) {
      if (history->item(i, 1)->text() == target) return history->item(i, 3)->text();
    }
    return {};
  }

  QString lastTarget() const {
    auto* history = field<QTableWidget>("operationHistory");
    return history->item(history->rowCount() - 1, 1)->text();
  }

  std::optional<OperationResult> reply(int index) {
    if (!until([&] { return results_.size() > index; })) return std::nullopt;
    auto result = results_.at(index);
    record("console_reply", {{"request_id", static_cast<qint64>(result.id)},
         {"outcome", static_cast<int>(result.outcome)}, {"message", result.message}});
    return result;
  }

  bool accepted(int index) {
    const auto result = reply(index);
    return result && result->outcome == OperationOutcome::Accepted;
  }

  std::pair<QString, OperationResult> submitWorkflow(bool start) {
    if (!trigger("loadWorkflows") || !accepted(0)) return {};
    auto* combo = field<QComboBox>("workflowChoice");
    int workflow = combo->findText("warehouse / inbound_crossdock", Qt::MatchStartsWith);
    if (workflow < 0) return {};
    combo->setCurrentIndex(workflow);
    field<QCheckBox>("workflowStart")->setChecked(start);
    if (!trigger("submitWorkflow")) return {};
    const auto order = field<QLineEdit>("workflowOrderId")->text();
    return {order, reply(1).value_or(OperationResult{})};
  }

  std::pair<QString, OperationResult> submitTransport(const QString& pickup, const QString& dropoff) {
    if (!readyStations(pickup, dropoff) || !trigger("submitTransport")) return {};
    const auto task = lastTarget();
    return {task, reply(0).value_or(OperationResult{})};
  }

  template<class Service, class Fill>
  std::shared_ptr<typename Service::Response> fixtureCall(const char* name, Fill fill) {
    auto client = fixture_->create_client<Service>(name);
    if (!client->wait_for_service(5s)) return {};
    auto request = std::make_shared<typename Service::Request>();
    fill(*request);
    auto future = client->async_send_request(request);
    if (executor_.spin_until_future_complete(future, 5s) != rclcpp::FutureReturnCode::SUCCESS) return {};
    record("fixture_call", {{"service", QString::fromLatin1(name)}});
    return future.get();
  }

  auto queryWorkflow(const QString& order) -> business::GetWorkflowStatus::Response::SharedPtr {
    return fixtureCall<business::GetWorkflowStatus>("/v2/get_workflow_status",
        [&](auto& request) { request.order_id = order.toStdString(); });
  }

  bool setFleetEnabled(const std::string& robot, bool enabled) {
    const auto response = fixtureCall<fleet::UpdateFleetRobotState>("/v2/update_fleet_robot_state",
        [&](auto& request) {
          request.robot_id = robot;
          request.update_enabled = true;
          request.enabled = enabled;
        });
    return response && response->success && response->enabled == enabled;
  }

  auto segment(const std::string& robot, const std::string& from, const std::string& to)
      -> mission::RequestRouteSegment::Response::SharedPtr {
    return fixtureCall<mission::RequestRouteSegment>("/v2/request_route_segment", [&](auto& request) {
      request.robot_id = robot;
      request.from_station = from;
      request.to_station = to;
      request.goal_station = to;
    });
  }

  bool reserve(const std::string& robot, const std::string& from, const std::string& to, bool granted) {
    const auto response = segment(robot, from, to);
    return response && (granted ? response->granted : response->queued);
  }

  bool release(const std::string& robot) {
    const auto response = fixtureCall<mission::ReleaseRouteSegment>("/v2/release_route_segment",
        [&](auto& request) { request.robot_id = robot; });
    return response && response->success;
  }

  QString owner(const QString& resource) const {
    return traffic_.owner_ids.value(traffic_.resource_ids.indexOf(resource));
  }

  bool pauseGroup(bool alpha_holds = false) {
    for (const auto& other : {"receiving", "packing"}) {
      const auto blocked = fixtureCall<mission::BlockStationRoute>("/v2/block_station_route",
          [&](auto& request) {
            request.from_station = "storage_b";
            request.to_station = other;
            request.reason = "ops_pause_fixture";
          });
      if (!blocked || !blocked->success) return false;
    }
    return (alpha_holds || reserve("alpha", "receiving", "storage_a", true)) &&
           reserve("beta", "packing", "storage_a", true) &&
           reserve("alpha", "storage_a", "packing", false) &&
           reserve("beta", "storage_a", "receiving", false) &&
           until([&] { return traffic_.last_resolution_action == QStringLiteral("pause"); });
  }

  auto previewClear(const QString& resource) -> std::optional<OperationResult> {
    auto* selector = field<QComboBox>("clearResource");
    if (!until([&] { return selector->findText(resource) >= 0; })) return std::nullopt;
    selector->setCurrentText(resource);
    const int i = results_.size();
    if (!trigger("previewClear")) return std::nullopt;
    return reply(i);
  }

  bool checkConflict(const QString& new_owner) {
    const int i = results_.size();
    if (!trigger("confirmClear")) return false;
    const auto conflict = reply(i);
    if (!conflict || conflict->outcome != OperationOutcome::Conflict) return false;
    const auto updated = reply(i + 1);
    return updated && updated->preview.found && updated->preview.owner_id == new_owner &&
           until([&] { return owner(kEdge) == new_owner; }) &&
           field<QPushButton>("confirmClear")->isEnabled() && results_.size() == i + 2;
  }

  rclcpp::Node::SharedPtr fixture_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  QTimer pump_;
  std::unique_ptr<RosBridge> bridge_;
  std::unique_ptr<MainWindow> window_;
  QVector<OperationResult> results_;
  OperatorSnapshotData snapshot_;
  TrafficStateData traffic_;
  QHash<QString, PoseSample> poses_;
};

int main(int argc, char** argv) {
  rclcpp::init(0, nullptr);
  QApplication app(argc, argv);
  ConsoleOpsAcceptance suite;
  const int result = QTest::qExec(&suite, argc, argv);
  rclcpp::shutdown();
  return result;
}

#include "console_ops_acceptance.moc"
