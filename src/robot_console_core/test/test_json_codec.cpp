#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

#include "robot_console_core/json_codec.hpp"
#include "test_common.hpp"

using namespace robot_console;

class TestJsonCodec : public QObject {
  Q_OBJECT

 private slots:
  void parsesEveryKindOfMini() {
    const QByteArray data = test::readData(QStringLiteral("mini.jsonl"));
    QVERIFY(!data.isEmpty());
    QSet<RecordKind> kinds;
    for (const QByteArray& line : data.split('\n')) {
      if (line.trimmed().isEmpty()) continue;
      ReplayRecord rec;
      QString err;
      QCOMPARE(parseRecordLine(line, &rec, &err), ParseResult::Ok);
      kinds.insert(rec.kind);
    }
    QCOMPARE(kinds.size(), 6);
  }

  void meta() {
    const auto m = std::get<ReplayMeta>(test::miniRecord(RecordKind::Meta).payload);
    QCOMPARE(m.v, 1);
    QCOMPARE(m.network, QStringLiteral("mini"));
    QCOMPARE(m.robots, 2);
    QCOMPARE(m.mode, QStringLiteral("rolling"));
  }

  void graph() {
    const StationGraph g = test::miniGraph();
    QCOMPARE(g.station_ids, (QStringList{"a", "b", "c"}));
    QCOMPARE(g.x.value(2), 4.0);
    QCOMPARE(g.y.value(2), 3.0);
    QCOMPARE(g.from_station.size(), 3);
    QCOMPARE(g.bidirectional, (QVector<bool>{true, false, false}));
    QCOMPARE(g.enabled, (QVector<bool>{true, true, false}));
  }

  void pose() {
    const ReplayRecord rec = test::miniRecord(RecordKind::Pose);
    const auto p = std::get<PoseSample>(rec.payload);
    QCOMPARE(rec.t, 0.1);
    QCOMPARE(p.robot, QStringLiteral("robot_2"));
    QCOMPARE(p.stamp, 1795000000.25);
    QCOMPARE(p.x, 1.2);
    QCOMPARE(p.vx, 0.5);
  }

  void traffic() {
    const TrafficStateData t = test::miniTraffic();
    QCOMPARE(t.stamp, 1795000001.5);
    QCOMPARE(t.resource_ids, (QStringList{"route_edge:a__b", "route_node:c"}));
    QCOMPARE(t.owner_ids, (QStringList{"robot_2", "robot_1"}));
    QCOMPARE(t.waiting_robot_ids, QStringList{"robot_1"});
    QCOMPARE(t.grant_count, quint64(12));
    QCOMPARE(t.last_resolution_ms, 412.5);
    QCOMPARE(t.reroute_count, quint64(1));
  }

  // 规格「字段名与 ROS 接口一致」：traffic 的 data 键与 TrafficState.msg 的字段一一对应。
  void trafficKeysMatchRosMessage() {
    QFile msg(test::dataPath(QStringLiteral("../../../robot_interfaces_mission/msg/TrafficState.msg")));
    if (!msg.open(QIODevice::ReadOnly | QIODevice::Text)) QSKIP("找不到 TrafficState.msg（不在完整仓库里）");
    QSet<QString> fields;
    static const QRegularExpression re(QStringLiteral("^\\s*[A-Za-z_/0-9]+(\\[\\])?\\s+([a-z_0-9]+)\\s*$"));
    for (const QString& line : QString::fromUtf8(msg.readAll()).split(QLatin1Char('\n'))) {
      const auto m = re.match(line);
      if (m.hasMatch()) fields.insert(m.captured(2));
    }
    QVERIFY(fields.size() > 10);

    QJsonObject data;
    for (const QByteArray& line : test::readData(QStringLiteral("mini.jsonl")).split('\n')) {
      const QJsonObject rec = QJsonDocument::fromJson(line).object();
      if (rec.value(QStringLiteral("kind")).toString() == QLatin1String("traffic")) {
        data = rec.value(QStringLiteral("data")).toObject();
        break;
      }
    }
    QSet<QString> keys;
    for (const QString& k : data.keys()) keys.insert(k);
    QCOMPARE(keys, fields);
  }

  void snapshot() {
    const OperatorSnapshotData s = test::miniSnapshot();
    QVERIFY(s.success);
    QCOMPARE(s.queue_size, 2);
    QCOMPARE(s.queued_priorities, (QVector<int>{5, 1}));
    QCOMPARE(s.fleet_battery_voltage.value(1), 23.85);
    QCOMPARE(s.remote_task_states, (QStringList{"FINISHED", "RUNNING"}));
  }

  void event() {
    const auto e = std::get<FleetEvent>(test::miniRecord(RecordKind::Event).payload);
    QCOMPARE(e.data.value(QStringLiteral("task")).toString(), QStringLiteral("t1"));
    QCOMPARE(e.data.value(QStringLiteral("wait_route_s")).toDouble(), 0.84);
  }

  void stampAcceptsSecondsOrTime() {
    ReplayRecord rec;
    QCOMPARE(parseRecordLine(R"({"t":1,"kind":"traffic","data":{"stamp":12.5}})", &rec), ParseResult::Ok);
    QCOMPARE(std::get<TrafficStateData>(rec.payload).stamp, 12.5);
  }

  void unknownKindAndInvalid() {
    ReplayRecord rec;
    QCOMPARE(parseRecordLine(R"({"t":1,"kind":"estop","data":{}})", &rec), ParseResult::UnknownKind);
    QCOMPARE(parseRecordLine("not json", &rec), ParseResult::Invalid);
    QCOMPARE(parseRecordLine(R"({"kind":"pose"})", &rec), ParseResult::Invalid);
  }
};

QTEST_GUILESS_MAIN(TestJsonCodec)
#include "test_json_codec.moc"
