#include <QtTest>

#include "robot_console_core/fleet_table_model.hpp"
#include "test_common.hpp"

using namespace robot_console;

class TestFleetModel : public QObject {
  Q_OBJECT

  static QString cell(const FleetTableModel& m, int row, int col) {
    return m.data(m.index(row, col)).toString();
  }

 private slots:
  void columnsAndVoltage() {
    FleetTableModel m;
    m.setSnapshot(test::miniSnapshot());
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.columnCount(), 6);
    QCOMPARE(cell(m, 0, FleetTableModel::kRobot), QStringLiteral("robot_1"));
    QCOMPARE(cell(m, 0, FleetTableModel::kEnabled), QStringLiteral("是"));
    QCOMPARE(cell(m, 1, FleetTableModel::kStation), QStringLiteral("a"));
    // 电压按伏特原样显示，不换算成百分比。
    QCOMPARE(cell(m, 1, FleetTableModel::kVoltage), QStringLiteral("23.85 V"));
    QCOMPARE(m.data(m.index(1, FleetTableModel::kVoltage), Qt::UserRole).toDouble(), 23.85);
  }

  // 规格「执行中的车」。
  void busyRobotShowsTask() {
    FleetTableModel m;
    m.setSnapshot(test::miniSnapshot());
    QCOMPARE(cell(m, 1, FleetTableModel::kState), QStringLiteral("busy"));
    QCOMPARE(cell(m, 1, FleetTableModel::kTask), QStringLiteral("t1"));
    QCOMPARE(cell(m, 0, FleetTableModel::kTask), QString());  // t0 已 FINISHED
  }

  // 规格「任务完成后清空」。
  void finishedTaskClears() {
    FleetTableModel m;
    OperatorSnapshotData s = test::miniSnapshot();
    m.setSnapshot(s);
    s.remote_task_states[1] = QStringLiteral("FINISHED");
    m.setSnapshot(s);
    QCOMPARE(cell(m, 1, FleetTableModel::kTask), QString());
  }

  void terminalStates() {
    QVERIFY(FleetTableModel::isTerminalTaskState(QStringLiteral("FAILED")));
    QVERIFY(FleetTableModel::isTerminalTaskState(QStringLiteral("CANCELED")));
    QVERIFY(!FleetTableModel::isTerminalTaskState(QStringLiteral("QUEUED")));
  }
};

QTEST_GUILESS_MAIN(TestFleetModel)
#include "test_fleet_model.moc"
