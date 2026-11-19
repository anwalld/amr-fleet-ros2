#include <QtTest>

#include "robot_console_core/task_queue_model.hpp"
#include "test_common.hpp"

using namespace robot_console;

class TestTaskModel : public QObject {
  Q_OBJECT

  static QString cell(const TaskQueueModel& m, int row, int col) { return m.data(m.index(row, col)).toString(); }

 private slots:
  // 规格「排队顺序」。
  void queueOrder() {
    TaskQueueModel m;
    m.setSnapshot(test::miniSnapshot());
    // 没有执行中的任务：前两行是排队，后两行是远程任务。
    QCOMPARE(m.rowCount(), 4);
    QCOMPARE(cell(m, 0, TaskQueueModel::kTaskId), QStringLiteral("m3"));
    QCOMPARE(cell(m, 0, TaskQueueModel::kPriority), QStringLiteral("5"));
    QCOMPARE(cell(m, 1, TaskQueueModel::kTaskId), QStringLiteral("m1"));
    QCOMPARE(cell(m, 1, TaskQueueModel::kPriority), QStringLiteral("1"));
    QCOMPARE(cell(m, 3, TaskQueueModel::kTaskId), QStringLiteral("t1"));
    QCOMPARE(cell(m, 3, TaskQueueModel::kRobot), QStringLiteral("robot_2"));
    QCOMPARE(cell(m, 3, TaskQueueModel::kState), QStringLiteral("RUNNING"));
  }

  void activeMissionFirstAndRefresh() {
    TaskQueueModel m;
    OperatorSnapshotData s = test::miniSnapshot();
    s.active_mission_id = QStringLiteral("m7");
    s.mission_state = QStringLiteral("RUNNING");
    m.setSnapshot(s);
    QCOMPARE(cell(m, 0, TaskQueueModel::kCategory), QStringLiteral("执行中"));
    QCOMPARE(cell(m, 0, TaskQueueModel::kState), QStringLiteral("RUNNING"));
    s.queued_mission_ids.clear();
    s.queued_priorities.clear();
    s.remote_task_ids.clear();
    m.setSnapshot(s);  // 整体按新快照刷新
    QCOMPARE(m.rowCount(), 1);
  }
};

QTEST_GUILESS_MAIN(TestTaskModel)
#include "test_task_model.moc"
