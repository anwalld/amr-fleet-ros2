#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/map_scene.hpp"
#include "robot_console_core/operation_source.hpp"
#include "robot_console_core/replay_source.hpp"

using namespace robot_console;

namespace {

class FakeOperations final : public OperationSource {
 public:
  bool available(Operation operation) const override { return !unavailable.contains(operation); }
  bool submit(const OperationRequest& request) override {
    requests.append(request);
    return true;
  }

  void answer(int index, OperationResult result) {
    result.id = requests.at(index).id;
    emit operationCompleted(result);
    QCoreApplication::processEvents();
  }

  QSet<Operation> unavailable;
  QVector<OperationRequest> requests;
};

template<class T>
T* field(QWidget& window, const char* name) {
  auto* control = window.findChild<T*>(QString::fromLatin1(name));
  Q_ASSERT(control);
  return control;
}

void click(QWidget& window, const char* name) { field<QPushButton>(window, name)->click(); }

OperationResult accepted() {
  OperationResult result;
  result.outcome = OperationOutcome::Accepted;
  return result;
}

void graph(ReplaySource& source) {
  StationGraph graph;
  graph.station_ids = {"a", "b"};
  graph.x = {0.0, 1.0};
  graph.y = {0.0, 0.0};
  graph.from_station = {"b"};
  graph.to_station = {"a"};
  emit source.graphReceived(graph);
}

void snapshot(ReplaySource& source, const QStringList& robots = {"r2", "r1"}) {
  OperatorSnapshotData data;
  data.success = true;
  data.fleet_robot_ids = robots;
  data.fleet_enabled = {true, true};
  emit source.snapshotReceived(data);
}

void traffic(ReplaySource& source, const QString& owner) {
  TrafficStateData data;
  data.mode = QStringLiteral("rolling");
  if (!owner.isEmpty()) {
    data.resource_ids = {"route_edge:a__b"};
    data.owner_ids = {owner};
  }
  emit source.trafficReceived(data);
}

QString cell(QWidget& window, int row, int column) {
  return field<QTableWidget>(window, "operationHistory")->item(row, column)->text();
}

}  // namespace

class TestOperations : public QObject {
  Q_OBJECT

 private slots:
  void replayHasNoOperations() {
    ReplaySource source;
    MainWindow window(&source);
    QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("submitWorkflow")));
    graph(source);
    QVERIFY(window.mapScene()->edge("a", "b"));
  }

  void remoteControlReconcilesOriginalTaskAfterReconnect() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    OperatorSnapshotData data;
    data.success = true;
    data.remote_task_ids = {QStringLiteral("task_a"), QStringLiteral("task_b")};
    emit source.snapshotReceived(data);
    auto* task = field<QComboBox>(window, "remoteTaskId");
    task->setCurrentText(QStringLiteral("task_a"));
    click(window, "remoteTaskAction0");
    QCOMPARE(operations.requests.size(), 1);
    auto running = accepted();
    running.remote_task_id = QStringLiteral("task_a");
    running.remote_state = QStringLiteral("RUNNING");
    running.execution_id = 91;
    running.latest_revision = 3;
    operations.answer(0, running);
    click(window, "remoteTaskAction1");
    QCOMPARE(operations.requests.size(), 2);
    QCOMPARE(operations.requests[1].execution_id, quint64{91});
    QCOMPARE(operations.requests[1].revision, quint64{4});
    auto pausing = running;
    pausing.revision = 4;
    pausing.remote_state = QStringLiteral("PAUSING");
    operations.answer(1, pausing);
    QCOMPARE(cell(window, 1, 3), QStringLiteral("PAUSING"));
    QVERIFY(!field<QPushButton>(window, "remoteTaskAction2")->isEnabled());
    operations.unavailable.insert(Operation::RemoteTaskControl);
    emit operations.availabilityChanged();
    QCoreApplication::processEvents();
    task->setCurrentText(QStringLiteral("task_b"));
    operations.unavailable.remove(Operation::RemoteTaskControl);
    emit operations.availabilityChanged();
    QTRY_COMPARE(operations.requests.size(), 3);
    const auto query = operations.requests[2];
    QCOMPARE(query.target, QStringLiteral("task_a"));
    QCOMPARE(query.execution_id, quint64{91});
    QCOMPARE(query.revision, quint64{4});
    QCOMPARE(query.remote_action, RemoteTaskAction::Query);
    auto paused = pausing;
    paused.remote_state = QStringLiteral("PAUSED");
    operations.answer(2, paused);
    QCOMPARE(cell(window, 1, 3), QStringLiteral("PAUSED"));
    QVERIFY(!field<QPushButton>(window, "remoteTaskAction2")->isEnabled());
  }

  void workflowAndCancelKeepSeparateReceipts() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    click(window, "loadWorkflows");
    auto directory = accepted();
    directory.workflows = {{"warehouse", "ship", "出库", 12}};
    operations.answer(0, directory);
    click(window, "submitWorkflow");
    QCOMPARE(operations.requests.size(), 2);
    const auto request = operations.requests[1];
    QCOMPARE(request.scenario_id, QStringLiteral("warehouse"));
    QCOMPARE(request.workflow_id, QStringLiteral("ship"));
    QCOMPARE(request.priority, 12);
    QVERIFY(request.start_if_idle);
    QVERIFY(!request.preempt_current);
    QVERIFY(!request.target.isEmpty());
    click(window, "submitWorkflow");
    QCOMPARE(operations.requests.size(), 2);

    auto submission = accepted();
    submission.mission_ids = {"mission_1", "mission_2"};
    operations.answer(1, submission);
    QCOMPARE(operations.requests.size(), 3);
    QCOMPARE(operations.requests[2].target, request.target);
    QVERIFY(cell(window, 1, 2).contains("mission_2"));

    auto running = accepted();
    running.workflow_state = "RUNNING";
    running.running_mission_id = "mission_1";
    operations.answer(2, running);
    field<QCheckBox>(window, "includeActive")->setChecked(true);

    QTimer::singleShot(0, &window, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box) box->reject();
    });
    click(window, "cancelWorkflow");
    QCOMPARE(operations.requests.size(), 3);

    QTimer::singleShot(0, &window, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box) box->button(QMessageBox::Yes)->click();
    });
    click(window, "cancelWorkflow");
    QCOMPARE(operations.requests.size(), 4);
    QCOMPARE(operations.requests[3].target, request.target);
    QVERIFY(operations.requests[3].include_active);
    auto canceled = accepted();
    canceled.mission_ids = {"mission_2"};
    canceled.active_requested = true;
    operations.answer(3, canceled);
    QCOMPARE(operations.requests.size(), 5);
    auto unknown = accepted();
    unknown.workflow_state = "UNKNOWN";
    operations.answer(4, unknown);
    QVERIFY(cell(window, 3, 2).contains("mission_2"));
    QVERIFY(field<QLabel>(window, "workflowStatus")->text().contains("UNKNOWN"));
    QVERIFY(!field<QPushButton>(window, "pauseWorkflow")->isEnabled());
  }

  void rejectsWorkflowWithBackendReason() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    click(window, "loadWorkflows");
    auto directory = accepted();
    directory.workflows = {{"warehouse", "ship", "出库", 12}};
    operations.answer(0, directory);
    click(window, "submitWorkflow");
    OperationResult rejection;
    rejection.message = "backend rejected workflow";
    operations.answer(1, rejection);
    QCOMPARE(operations.requests.size(), 2);
    QVERIFY(cell(window, 1, 2).contains(rejection.message));
  }

  void workflowSelectionSurvivesReorderAndExpiresWhenMissionsDisappear() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    auto* order = field<QLineEdit>(window, "workflowOrderId");
    order->setText("order_7");
    click(window, "queryWorkflow");
    auto status = accepted();
    status.workflow_state = "QUEUED";
    status.mission_ids = {"mission_1", "mission_2"};
    operations.answer(0, status);
    OperatorSnapshotData data;
    data.success = true;
    data.queued_mission_ids = {"mission_2", "unrelated", "mission_1"};
    data.remote_task_ids = {"remote_1"};
    data.remote_task_robot_ids = {"r2"};
    data.remote_task_states = {"RUNNING"};
    emit source.snapshotReceived(data);
    click(window, "pauseWorkflow");
    QCOMPARE(operations.requests.size(), 2);
    QCOMPARE(operations.requests[1].target, QStringLiteral("order_7"));
    OperationResult reject;
    reject.message = "no longer queued";
    operations.answer(1, reject);
    data.queued_mission_ids = {"unrelated"};
    emit source.snapshotReceived(data);
    QVERIFY(!field<QPushButton>(window, "pauseWorkflow")->isEnabled());
    QVERIFY(!field<QPushButton>(window, "resumeWorkflow")->isEnabled());
    QVERIFY(!field<QPushButton>(window, "cancelWorkflow")->isEnabled());
    QVERIFY(field<QLabel>(window, "workflowStatus")->text().contains("重新查询"));
  }

  void transportObservesRemoteAndLocalSeparately() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    graph(source);
    field<QComboBox>(window, "pickupStation")->setCurrentText("a");
    field<QComboBox>(window, "dropoffStation")->setCurrentText("b");
    click(window, "submitTransport");
    QCOMPARE(operations.requests.size(), 1);
    const auto remote = operations.requests[0].target;
    auto data = OperatorSnapshotData{};
    data.success = true;
    data.remote_task_ids = {remote};
    data.remote_task_robot_ids = {"r2"};
    data.remote_task_states = {"COMPLETED"};
    emit source.snapshotReceived(data);
    QVERIFY(cell(window, 0, 2).contains("提交中"));
    QVERIFY(cell(window, 0, 3).contains("COMPLETED"));

    OperationResult timeout;
    timeout.outcome = OperationOutcome::Unknown;
    operations.answer(0, timeout);
    QVERIFY(cell(window, 0, 2).contains("结果未知"));
    QCOMPARE(operations.requests.size(), 1);
    click(window, "submitTransport");
    QCOMPARE(operations.requests.size(), 2);
    const auto local = operations.requests[1].target;
    QVERIFY(local != remote);
    operations.answer(0, accepted());
    QVERIFY(cell(window, 1, 2).contains("提交中"));
    auto receipt = accepted();
    receipt.mission_id = "mission_local";
    receipt.assigned_robot_id = "r1";
    operations.answer(1, receipt);
    data.remote_task_ids.clear();
    data.queued_mission_ids = {"mission_local"};
    emit source.snapshotReceived(data);
    QVERIFY(cell(window, 1, 2).contains("r1"));
    QVERIFY(cell(window, 1, 3).contains("mission_local"));

    click(window, "submitTransport");
    QCOMPARE(operations.requests.size(), 3);
    window.close();
  }

  void routeAndRobotUseStableIds() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    graph(source);
    field<QComboBox>(window, "routeTarget")->setCurrentText("route_edge:a__b");
    traffic(source, "r1");
    click(window, "blockRoute");
    QCOMPARE(operations.requests.size(), 1);
    QCOMPARE(operations.requests[0].from_station, QStringLiteral("b"));
    QCOMPARE(operations.requests[0].to_station, QStringLiteral("a"));
    OperationResult rejection;
    rejection.message = "occupied by r1";
    operations.answer(0, rejection);
    QVERIFY(cell(window, 0, 2).contains(rejection.message));

    click(window, "blockRoute");
    QCOMPARE(operations.requests.size(), 2);
    traffic(source, "traffic_block:maintenance");
    QCOMPARE(window.mapScene()->edge("a", "b")->line->pen().color(), MapScene::blockColor());
    QVERIFY(cell(window, 1, 3).contains("已观测封路"));

    snapshot(source);
    field<QComboBox>(window, "robotTarget")->setCurrentText("r2");
    snapshot(source, {"r1", "r2"});
    field<QCheckBox>(window, "robotEnabled")->setChecked(false);
    click(window, "setRobotEnabled");
    QCOMPARE(operations.requests.size(), 3);
    QCOMPARE(operations.requests[2].target, QStringLiteral("r2"));
    QVERIFY(!operations.requests[2].enabled);
  }

  void conflictsRequireAnotherConfirmation() {
    ReplaySource source;
    FakeOperations operations;
    MainWindow window(&source, {}, nullptr, &operations);
    traffic(source, "r1");
    field<QComboBox>(window, "clearResource")->setCurrentText("route_edge:a__b");
    operations.unavailable.insert(Operation::ClearReservation);
    emit operations.availabilityChanged();
    QCoreApplication::processEvents();
    click(window, "previewClear");
    QCOMPARE(operations.requests.size(), 0);
    operations.unavailable = {Operation::PreviewClear};
    emit operations.availabilityChanged();
    QCoreApplication::processEvents();
    click(window, "previewClear");
    click(window, "confirmClear");
    QCOMPARE(operations.requests.size(), 0);
    operations.unavailable.clear();
    emit operations.availabilityChanged();
    QCoreApplication::processEvents();

    click(window, "previewClear");
    QCOMPARE(operations.requests.size(), 1);
    auto preview = accepted();
    preview.preview = {true, "r1", {"route_edge:a__b", "route_node:b"}, {"r1", "r2"}};
    operations.answer(0, preview);
    auto* display = field<QLabel>(window, "clearPreview");
    QVERIFY(display->text().contains("route_node:b"));
    QVERIFY(display->text().contains("r2"));
    click(window, "confirmClear");
    QCOMPARE(operations.requests.size(), 2);
    QCOMPARE(operations.requests[1].preview.resource_ids, preview.preview.resource_ids);
    QCOMPARE(operations.requests[1].preview.resume_robot_ids, preview.preview.resume_robot_ids);
    OperationResult conflict;
    conflict.outcome = OperationOutcome::Conflict;
    operations.answer(1, conflict);
    QCOMPARE(operations.requests.size(), 3);
    QCOMPARE(operations.requests[2].operation, Operation::PreviewClear);
    traffic(source, "r2");
    preview.preview.owner_id = "r2";
    operations.answer(2, preview);
    QVERIFY(display->text().contains("r2"));
    QCOMPARE(operations.requests.size(), 3);
    click(window, "confirmClear");
    QCOMPARE(operations.requests.size(), 4);
    QCOMPARE(operations.requests[3].preview.owner_id, QStringLiteral("r2"));
    operations.answer(3, conflict);
    traffic(source, {});
    operations.answer(4, accepted());
    QVERIFY(!field<QPushButton>(window, "confirmClear")->isEnabled());
    QCOMPARE(operations.requests.size(), 5);
  }
};

QTEST_MAIN(TestOperations)
#include "test_operations.moc"
