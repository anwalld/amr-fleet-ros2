#include <QtTest>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/replay_source.hpp"
#include "robot_console_core/traffic_panel.hpp"
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

  void replaysRing3UntilResolved() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    MainWindow window(&src, clock.fn());
    auto* panel = window.trafficPanel();
    QString error;
    QVERIFY2(src.load(test::dataPath(QStringLiteral("ring3.jsonl")), &error), qPrintable(error));
    QCOMPARE(src.skippedLines(), quint64(0));

    const auto threeRobots = QStringLiteral("robot_1, robot_2, robot_3");
    const auto noRing = QStringLiteral("无");
    const auto unresolved = QStringLiteral("尚未解开");
    bool sawBeforeRing = false;
    bool sawRing = false;
    bool sawResolution = false;
    quint64 resolvedAtDetection = 0;
    connect(&src, &DataSource::trafficReceived, &window, [&] {
      const auto ring = panel->fieldText(QStringLiteral("currentRing"));
      const auto resolution = panel->fieldText(QStringLiteral("lastResolutionMs"));
      const auto resolved = panel->fieldText(QStringLiteral("resolvedCount")).toULongLong();
      if (!sawRing && ring == noRing && resolution == unresolved) {
        sawBeforeRing = true;
      }
      if (!sawRing && sawBeforeRing && ring == threeRobots && resolution == unresolved) {
        sawRing = true;
        resolvedAtDetection = resolved;
      }
      if (sawRing && ring == noRing && resolved > resolvedAtDetection &&
          resolution.endsWith(QStringLiteral(" ms"))) {
        sawResolution = true;
      }
    });

    src.start();
    clock.advance(qCeil(src.duration() * 1000) + 1);
    src.tick();
    QVERIFY(src.isFinished());
    QVERIFY(sawBeforeRing);
    QVERIFY(sawRing);
    QVERIFY(sawResolution);
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
