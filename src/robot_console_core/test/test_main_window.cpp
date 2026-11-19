#include <QtTest>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/replay_source.hpp"
#include "test_common.hpp"

using namespace robot_console;

class TestMainWindow : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { registerMetaTypes(); }

  void showsSnapshotFields() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    MainWindow w(&src, clock.fn());
    w.show();
    QVERIFY(w.statusText().contains(QStringLiteral("尚无快照")));
    OperatorSnapshotData s = test::miniSnapshot();
    s.mission_state = QStringLiteral("RUNNING");
    s.active_mission_id = QStringLiteral("m9");
    emit src.snapshotReceived(s);
    const QString text = w.statusText();
    QVERIFY2(text.contains(QStringLiteral("RUNNING")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("m9")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("队列 2")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("回放")), qPrintable(text));
  }

  // 规格「快照过期」。
  void marksStaleSnapshot() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    MainWindow w(&src, clock.fn());
    emit src.snapshotReceived(test::miniSnapshot());
    clock.advance(2000);
    w.refreshStatus();
    QVERIFY(!w.statusText().contains(QStringLiteral("快照过期")));
    clock.advance(2000);  // 共 4 秒
    w.refreshStatus();
    const QString text = w.statusText();
    QVERIFY2(text.contains(QStringLiteral("快照过期（4.0 s）")), qPrintable(text));
  }
};

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
