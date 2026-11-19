#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QtTest>

#include <cmath>

#include "robot_console_core/map_scene.hpp"
#include "robot_console_core/map_view.hpp"
#include "robot_console_core/smoke_metrics.hpp"
#include "test_common.hpp"

using namespace robot_console;

class TestMapScene : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { registerMetaTypes(); }

  // 规格「地图只画路网」。
  void drawsNetwork() {
    MapScene scene;
    scene.setGraph(test::miniGraph());
    QVERIFY(scene.stationItem(QStringLiteral("a")));
    QVERIFY(scene.stationItem(QStringLiteral("c")));
    QCOMPARE(scene.edgeCount(), 3);
    // 地图坐标 (4, 3) 放在场景 (4, -3)，屏幕上 y 轴朝上。
    QCOMPARE(scene.stationScenePos(QStringLiteral("c")), QPointF(4.0, -3.0));

    const auto* ab = scene.edge(QStringLiteral("a"), QStringLiteral("b"));
    const auto* bc = scene.edge(QStringLiteral("b"), QStringLiteral("c"));
    const auto* ca = scene.edge(QStringLiteral("c"), QStringLiteral("a"));
    QVERIFY(ab && bc && ca);
    QVERIFY(ab->arrow == nullptr);  // 双向，无箭头
    QVERIFY(bc->arrow != nullptr);  // 单向，带箭头
    QCOMPARE(ab->line->pen().style(), Qt::SolidLine);
    QCOMPARE(ca->line->pen().style(), Qt::DashLine);  // enabled=false 画虚线
  }

  // 规格「没有底图」：视图正好装下全部站点。
  void viewFitsAllStations() {
    MapScene scene;
    MapView view(&scene);
    view.resize(800, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    scene.setGraph(test::miniGraph());
    QCoreApplication::processEvents();
    const QRectF visible = view.mapToScene(view.viewport()->rect()).boundingRect();
    QVERIFY2(visible.contains(scene.stationsRect()), "视图应包含全部站点");
    // 站点占满视图的大部分（不是缩得很小）。
    QVERIFY(scene.stationsRect().width() > 0.5 * visible.width() ||
            scene.stationsRect().height() > 0.5 * visible.height());
  }

  // 规格「位置更新」，外加"没收到位置的车不画"。
  void robotPoseAndHeading() {
    MapScene scene;
    scene.setGraph(test::miniGraph());
    QVERIFY(scene.robotItem(QStringLiteral("robot_3")) == nullptr);
    PoseSample p;
    p.robot = QStringLiteral("robot_3");
    p.x = 2.0;
    p.y = 1.0;
    p.yaw = 1.5708;
    scene.setPose(p);
    QVERIFY(scene.robotItem(QStringLiteral("robot_3")) == nullptr);  // 合并刷新前不画
    scene.flushPending();
    auto* item = scene.robotItem(QStringLiteral("robot_3"));
    QVERIFY(item);
    QCOMPARE(item->pos(), QPointF(2.0, -1.0));
    // 车头（本地 +x）旋转后指向场景 -y，即屏幕上方、地图 y 轴正方向。
    const QPointF heading = QTransform().rotate(item->rotation()).map(QPointF(1, 0));
    QVERIFY(std::abs(heading.x()) < 1e-3);
    QVERIFY(heading.y() < -0.999);
  }

  // 任务 4.5：offscreen 下确实发生绘制，并记延迟样本。
  void paintRecordsLatency() {
    MapScene scene;
    MapView view(&scene);
    view.resize(640, 480);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    scene.setGraph(test::miniGraph());
    SmokeMetrics metrics;
    metrics.begin();
    view.setMetrics(&metrics);

    PoseSample p;
    p.robot = QStringLiteral("robot_1");
    p.stamp = systemNowSeconds() - 0.05;
    scene.setPose(p);
    scene.flushPending();
    const quint64 frames_before = view.frameCount();
    view.viewport()->repaint();
    QVERIFY(view.frameCount() > frames_before);
    QCOMPARE(metrics.latencySamples().size(), 1);
    // 系统时间只有毫秒精度，0.05 秒的差值在双精度下可能算成 49.999… ms。
    QVERIFY(metrics.latencySamples().first() >= 49.0);
    QVERIFY(metrics.frames() >= 1);

    // 本帧位置没有更新的车辆不记样本。
    view.viewport()->repaint();
    QCOMPARE(metrics.latencySamples().size(), 1);

    // 与系统时间相差超过 10 秒：不记样本，计入 latency_skipped。
    p.stamp = systemNowSeconds() - 20.0;
    scene.setPose(p);
    scene.flushPending();
    view.viewport()->repaint();
    QCOMPARE(metrics.latencySamples().size(), 1);
    QCOMPARE(metrics.latencySkipped(), quint64(1));
  }
};

QTEST_MAIN(TestMapScene)
#include "test_map_scene.moc"
