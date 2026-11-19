#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QtTest>

#include "robot_console_core/map_scene.hpp"
#include "robot_console_core/traffic_models.hpp"
#include "robot_console_core/traffic_panel.hpp"
#include "test_common.hpp"

using namespace robot_console;

namespace {

TrafficStateData occupancy(const QStringList& resources, const QStringList& owners) {
  TrafficStateData t;
  t.resource_ids = resources;
  t.owner_ids = owners;
  return t;
}

void place(MapScene& scene, const QString& robot, double x, double y) {
  PoseSample p;
  p.robot = robot;
  p.x = x;
  p.y = y;
  scene.setPose(p);
}

}  // namespace

class TestTraffic : public QObject {
  Q_OBJECT

 private slots:
  // 规格「车辆占用路段」与「释放后恢复」。
  void edgeColoredByOwnerThenRestored() {
    MapScene scene;
    scene.setGraph(test::miniGraph());
    scene.setTraffic(occupancy({QStringLiteral("route_edge:a__b")}, {QStringLiteral("robot_2")}));
    const auto* ab = scene.edge(QStringLiteral("a"), QStringLiteral("b"));
    QCOMPARE(ab->line->pen().color(), MapScene::robotColor(QStringLiteral("robot_2")));
    // 同一车辆颜色不变，不同车辆颜色不同。
    QCOMPARE(MapScene::robotColor(QStringLiteral("robot_2")), MapScene::robotColor(QStringLiteral("robot_2")));
    QVERIFY(MapScene::robotColor(QStringLiteral("robot_1")) != MapScene::robotColor(QStringLiteral("robot_2")));

    scene.setTraffic(TrafficStateData{});
    QCOMPARE(ab->line->pen().color(), MapScene::defaultEdgeColor());
  }

  void nodeBlockAndInvalid() {
    MapScene scene;
    scene.setGraph(test::miniGraph());
    scene.setTraffic(occupancy({QStringLiteral("route_node:c"), QStringLiteral("route_edge:b__c"),
                                QStringLiteral("garbage")},
                               {QStringLiteral("robot_1"), QStringLiteral("traffic_block:maintenance"),
                                QStringLiteral("robot_3")}));
    QCOMPARE(scene.stationItem(QStringLiteral("c"))->brush().color(), MapScene::robotColor(QStringLiteral("robot_1")));
    const auto* bc = scene.edge(QStringLiteral("b"), QStringLiteral("c"));
    QCOMPARE(bc->line->pen().color(), MapScene::blockColor());
    QCOMPARE(bc->line->pen().style(), Qt::DashDotLine);
    QCOMPARE(scene.invalidResourceCount(), quint64(1));
  }

  // 规格「三车成环」，以及条件消失后清除。
  void waitLinesAndDeadlockHighlight() {
    MapScene scene;
    scene.setGraph(test::miniGraph());
    place(scene, QStringLiteral("robot_1"), 0.5, 0.0);
    place(scene, QStringLiteral("robot_2"), 4.0, 0.5);
    place(scene, QStringLiteral("robot_3"), 3.5, 2.5);
    scene.flushPending();

    TrafficStateData t;
    t.resource_ids = {QStringLiteral("route_edge:a__b"), QStringLiteral("route_edge:b__c"), QStringLiteral("route_node:c")};
    t.owner_ids = {QStringLiteral("robot_1"), QStringLiteral("robot_2"), QStringLiteral("robot_3")};
    t.waiting_resource_ids = {QStringLiteral("route_edge:b__c"), QStringLiteral("route_node:c"),
                              QStringLiteral("route_edge:a__b")};
    t.waiting_robot_ids = {QStringLiteral("robot_1"), QStringLiteral("robot_2"), QStringLiteral("robot_3")};
    t.waiting_priorities = {0, 0, 0};
    t.waiting_positions = {0, 0, 0};
    t.deadlock_robot_ids = {QStringLiteral("robot_1"), QStringLiteral("robot_2"), QStringLiteral("robot_3")};
    scene.setTraffic(t);

    QCOMPARE(scene.waitLineCount(), 3);
    const auto lines = scene.waitLines();
    // robot_1 指向 b—c 的中点 (4, 1.5) → 场景 (4, -1.5)。
    QCOMPARE(lines[0]->line().p1(), QPointF(0.5, 0.0));
    QCOMPARE(lines[0]->line().p2(), QPointF(4.0, -1.5));
    QCOMPARE(lines[1]->line().p2(), scene.stationScenePos(QStringLiteral("c")));
    for (const QString r : {"robot_1", "robot_2", "robot_3"}) QVERIFY(scene.isRobotHighlighted(r));

    scene.setTraffic(TrafficStateData{});
    QCOMPARE(scene.waitLineCount(), 0);
    QVERIFY(!scene.isRobotHighlighted(QStringLiteral("robot_1")));
  }

  // 规格「尚未解开」。
  void panelShowsUnresolved() {
    TrafficPanel panel;
    TrafficStateData t = test::miniTraffic();
    panel.setTraffic(t);
    QCOMPARE(panel.fieldText(QStringLiteral("lastResolutionMs")), QStringLiteral("412.5 ms"));
    QCOMPARE(panel.fieldText(QStringLiteral("actionCounts")), QStringLiteral("1 / 0 / 0"));
    t.last_resolution_ms = -1.0;
    t.deadlock_robot_ids = {QStringLiteral("robot_1"), QStringLiteral("robot_2")};
    panel.setTraffic(t);
    QCOMPARE(panel.fieldText(QStringLiteral("lastResolutionMs")), QStringLiteral("尚未解开"));
    QCOMPARE(panel.fieldText(QStringLiteral("currentRing")), QStringLiteral("robot_1, robot_2"));
    QCOMPARE(panel.occupancyModel()->rowCount(), 2);
  }

  void waitQueueSortedByPosition() {
    WaitQueueModel m;
    TrafficStateData t;
    t.waiting_resource_ids = {QStringLiteral("r2"), QStringLiteral("r1"), QStringLiteral("r2"), QStringLiteral("r1")};
    t.waiting_robot_ids = {QStringLiteral("robot_4"), QStringLiteral("robot_2"), QStringLiteral("robot_3"),
                           QStringLiteral("robot_1")};
    t.waiting_priorities = {1, 0, 5, 0};
    t.waiting_positions = {1, 1, 0, 0};
    m.setTraffic(t);
    QCOMPARE(m.rowCount(), 4);
    QStringList robots;
    for (const auto& r : m.rows()) robots << r.robot;
    // 资源保持首次出现的顺序（r2 在前），同一资源内按队内位置。
    QCOMPARE(robots, (QStringList{"robot_3", "robot_4", "robot_1", "robot_2"}));
  }
};

QTEST_MAIN(TestTraffic)
#include "test_traffic.moc"
