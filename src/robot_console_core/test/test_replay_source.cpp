#include <QSignalSpy>
#include <QtTest>

#include "robot_console_core/replay_source.hpp"
#include "test_common.hpp"

using namespace robot_console;

namespace {

QByteArray metaLine(int v = 1) {
  return QByteArray(R"({"t":0,"kind":"meta","v":)") + QByteArray::number(v) + R"(,"network":"x","robots":1})";
}

QByteArray poseLine(double t) {
  return QByteArray(R"({"t":)") + QByteArray::number(t) + R"(,"kind":"pose","robot":"robot_1","stamp":1,"x":)" +
         QByteArray::number(t) + R"(,"y":0,"yaw":0,"vx":0,"wz":0})";
}

// 0–60 秒每秒一条位置。
QByteArray sixtySeconds() {
  QByteArray d = metaLine() + '\n';
  for (int i = 0; i <= 60; ++i) d += poseLine(i) + '\n';
  return d;
}

}  // namespace

class TestReplaySource : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { registerMetaTypes(); }

  void loadsMini() {
    ReplaySource src;
    QString err;
    QVERIFY2(src.load(test::dataPath(QStringLiteral("mini.jsonl")), &err), qPrintable(err));
    QCOMPARE(src.meta().network, QStringLiteral("mini"));
    QCOMPARE(src.recordCount(), 7);
    QCOMPARE(src.skippedLines(), quint64(0));
    QCOMPARE(src.duration(), 3.0);
  }

  // 规格「缺少 meta 头」。
  void rejectsMissingMeta() {
    ReplaySource src;
    QString err;
    QVERIFY(!src.loadFromData(poseLine(0) + '\n' + metaLine(), &err));
    QVERIFY2(err.contains(QStringLiteral("meta")), qPrintable(err));
  }

  void rejectsNewerVersion() {
    ReplaySource src;
    QString err;
    QVERIFY(!src.loadFromData(metaLine(2) + '\n' + poseLine(0), &err));
    QVERIFY2(err.contains(QStringLiteral("v=2")), qPrintable(err));
  }

  // 规格「未知 kind 被跳过」，外加坏行。
  void skipsUnknownKindAndBadLines() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    QString err;
    const QByteArray data = metaLine() + '\n' + poseLine(0) + "\n" + R"({"t":0.5,"kind":"estop","data":{}})" +
                            "\nnot json\n" + poseLine(1) + '\n';
    QVERIFY(src.loadFromData(data, &err));
    QCOMPARE(src.skippedLines(), quint64(2));
    QCOMPARE(src.recordCount(), 2);
    QSignalSpy spy(&src, &DataSource::poseReceived);
    src.start();
    clock.advance(1000);
    src.tick();
    QCOMPARE(spy.count(), 2);
  }

  // 规格「4 倍速播放」。
  void fourTimesSpeed() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    QString err;
    QVERIFY(src.loadFromData(sixtySeconds(), &err));
    QSignalSpy poses(&src, &DataSource::poseReceived);
    QSignalSpy done(&src, &ReplaySource::finished);
    src.setSpeed(4.0);
    src.start();
    for (int i = 0; i < 149; ++i) {  // 14.9 秒
      clock.advance(100);
      src.tick();
    }
    QCOMPARE(done.count(), 0);
    QVERIFY(poses.count() < 61);
    clock.advance(110);  // 约 15.0 秒（留 10 ms 容差，避免浮点累加误差）
    src.tick();
    QCOMPARE(poses.count(), 61);
    QCOMPARE(done.count(), 1);
    // 顺序与文件一致。
    for (int i = 0; i < poses.count(); ++i) {
      QCOMPARE(poses.at(i).at(0).value<PoseSample>().x, double(i));
    }
  }

  // 规格「暂停」。
  void pauseAndResume() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    QString err;
    QVERIFY(src.loadFromData(sixtySeconds(), &err));
    QSignalSpy poses(&src, &DataSource::poseReceived);
    src.start();
    clock.advance(5000);
    src.tick();
    QCOMPARE(poses.count(), 6);  // t = 0..5
    src.pause();
    clock.advance(10000);
    src.tick();
    QCOMPARE(poses.count(), 6);
    src.resume();
    clock.advance(1000);
    src.tick();
    QCOMPARE(poses.count(), 7);  // 从暂停处接着播
    QCOMPARE(poses.last().at(0).value<PoseSample>().x, 6.0);
  }

  void loopReemitsGraphFirst() {
    test::FakeClock clock;
    ReplaySource src(nullptr, clock.fn());
    QString err;
    QVERIFY(src.load(test::dataPath(QStringLiteral("mini.jsonl")), &err));
    src.setLoop(true);
    QStringList order;
    connect(&src, &DataSource::graphReceived, this, [&] { order << QStringLiteral("graph"); });
    connect(&src, &DataSource::poseReceived, this, [&] { order << QStringLiteral("pose"); });
    connect(&src, &DataSource::trafficReceived, this, [&] { order << QStringLiteral("traffic"); });
    connect(&src, &DataSource::snapshotReceived, this, [&] { order << QStringLiteral("snapshot"); });
    connect(&src, &DataSource::eventReceived, this, [&] { order << QStringLiteral("event"); });
    src.start();
    clock.advance(3000);
    src.tick();
    // 第一圈 7 条全部发完，回到开头时先补发路网。
    QCOMPARE(order.size(), 8);
    QCOMPARE(order.first(), QStringLiteral("graph"));
    QCOMPARE(order.last(), QStringLiteral("graph"));
    order.clear();
    clock.advance(1000);  // 第二圈的第 1 秒：路网不再重复，接着发后面的记录
    src.tick();
    QCOMPARE(order, (QStringList{"pose", "pose", "traffic"}));
    QVERIFY(!src.isFinished());
  }
};

QTEST_GUILESS_MAIN(TestReplaySource)
#include "test_replay_source.moc"
