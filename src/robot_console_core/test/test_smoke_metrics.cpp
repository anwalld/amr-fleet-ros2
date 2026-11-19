#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/replay_source.hpp"
#include "robot_console_core/smoke_metrics.hpp"
#include "robot_console_core/smoke_run.hpp"
#include "test_common.hpp"

using namespace robot_console;

class TestSmokeMetrics : public QObject {
  Q_OBJECT

  static QHash<QString, QString> asMap(const QVector<QPair<QString, QString>>& rows) {
    QHash<QString, QString> m;
    for (const auto& r : rows) m.insert(r.first, r.second);
    return m;
  }

 private slots:
  void initTestCase() { registerMetaTypes(); }

  void percentiles() {
    QVector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(i);
    QCOMPARE(SmokeMetrics::percentile(v, 50), 50.0);
    QCOMPARE(SmokeMetrics::percentile(v, 95), 95.0);
    QCOMPARE(SmokeMetrics::percentile({7.0}, 95), 7.0);
    QCOMPARE(SmokeMetrics::percentile({}, 50), 0.0);
  }

  void ignoresDataBeforeBegin() {
    SmokeMetrics m;
    m.countPose(QStringLiteral("robot_1"));
    m.countTraffic();
    QCOMPARE(m.poseCount(QStringLiteral("robot_1")), quint64(0));
    m.begin();
    m.countTraffic();
    QCOMPARE(m.trafficMsgs(), quint64(1));
  }

  void ratesAndMinPerRobot() {
    SmokeMetrics m;
    m.begin();
    m.countSnapshot({QStringLiteral("robot_1"), QStringLiteral("robot_2"), QStringLiteral("robot_3")});
    for (int i = 0; i < 100; ++i) m.countPose(QStringLiteral("robot_1"));
    for (int i = 0; i < 50; ++i) m.countPose(QStringLiteral("robot_2"));
    // robot_3 在快照里但没有位置：最低频率是 0。
    QCOMPARE(m.minPoseHz(10.0), 0.0);
    QCOMPARE(m.robotsWithPose(), 2);
    for (int i = 0; i < 30; ++i) m.countPose(QStringLiteral("robot_3"));
    QCOMPARE(m.minPoseHz(10.0), 3.0);
    m.addFrame(1.0);
    m.addLatencyMs(40.0);
    m.addLatencyMs(60.0);
    const auto map = asMap(m.results(10.0, 2));
    QCOMPARE(map.value(QStringLiteral("snapshot_hz")), QStringLiteral("0.10"));
    QCOMPARE(map.value(QStringLiteral("robots")), QStringLiteral("3"));
    QCOMPARE(map.value(QStringLiteral("pose_hz_min")), QStringLiteral("3.00"));
    QCOMPARE(map.value(QStringLiteral("pose_hz.robot_1")), QStringLiteral("10.00"));
    QCOMPARE(map.value(QStringLiteral("service_timeouts")), QStringLiteral("2"));
    QCOMPARE(map.value(QStringLiteral("latency_p50_ms")), QStringLiteral("40.00"));
    QCOMPARE(map.value(QStringLiteral("latency_max_ms")), QStringLiteral("60.00"));
    QCOMPARE(map.value(QStringLiteral("fps")), QStringLiteral("0.10"));
    for (const char* key : {"traffic_hz", "event_hz", "latency_p95_ms", "latency_skipped", "frames", "paint_p95_ms"}) {
      QVERIFY2(map.contains(QLatin1String(key)), key);
    }
  }

  // 规格「栈没起来」：等待超时退出码 2，不写文件。
  void waitTimeoutExitCode2() {
    QTemporaryDir dir;
    const QString out = dir.filePath(QStringLiteral("m.tsv"));
    ReplaySource src;  // 不加载、不开始：什么都不发
    SmokeRun run(&src, nullptr, 1.0, 0.2, out);
    QSignalSpy done(&run, &SmokeRun::done);
    run.start();
    QVERIFY(done.wait(3000));
    QCOMPARE(done.first().first().toInt(), 2);
    QVERIFY(!QFile::exists(out));
  }

  void runWritesTsvAgainstReplay() {
    QTemporaryDir dir;
    const QString out = dir.filePath(QStringLiteral("m.tsv"));
    ReplaySource src;
    QString err;
    QVERIFY(src.load(test::dataPath(QStringLiteral("mini.jsonl")), &err));
    src.setSpeed(4.0);
    MainWindow w(&src);
    w.show();
    SmokeRun run(&src, w.mapView(), 0.3, 5.0, out);
    QSignalSpy done(&run, &SmokeRun::done);
    run.start();
    src.start();
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().first().toInt(), 0);
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(f.readAll());
    QVERIFY(text.startsWith(QStringLiteral("metric\tvalue\n")));
    QVERIFY(text.contains(QStringLiteral("\nsnapshots\t")));
    QVERIFY(text.contains(QStringLiteral("\nlatency_p95_ms\t")));
  }
};

QTEST_MAIN(TestSmokeMetrics)
#include "test_smoke_metrics.moc"
