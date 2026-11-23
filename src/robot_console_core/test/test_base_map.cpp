#include <QFile>
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "robot_console_core/base_map.hpp"
#include "robot_console_core/map_scene.hpp"

using namespace robot_console;

class TestBaseMap : public QObject {
  Q_OBJECT

 private slots:
  // 规格「底图对齐」：resolution=0.05、origin=[-10,-5,0]，站点 (0,0) 在像素列 200、距底边 100 像素。
  void alignsWithOrigin() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage img(400, 300, QImage::Format_Grayscale8);
    img.fill(254);
    QVERIFY(img.save(dir.filePath(QStringLiteral("map.pgm")), "PGM"));
    QFile yaml(dir.filePath(QStringLiteral("map.yaml")));
    QVERIFY(yaml.open(QIODevice::WriteOnly | QIODevice::Text));
    yaml.write("image: map.pgm\nmode: trinary\nresolution: 0.05\norigin: [-10.0, -5.0, 0.0]\n"
               "negate: 0\noccupied_thresh: 0.65\nfree_thresh: 0.25\n");
    yaml.close();

    BaseMapInfo info;
    QString err;
    QVERIFY2(loadBaseMap(yaml.fileName(), &info, &err), qPrintable(err));
    QCOMPARE(info.resolution, 0.05);
    QCOMPARE(info.origin_x, -10.0);
    QCOMPARE(info.origin_y, -5.0);
    QCOMPARE(info.image.size(), QSize(400, 300));

    MapScene scene;
    scene.setBaseMap(info);
    QVERIFY(scene.baseMapItem());
    const QPointF px = scene.baseMapItem()->mapFromScene(MapScene::toScene(0.0, 0.0));
    QVERIFY(std::abs(px.x() - 200.0) < 1e-6);
    QVERIFY(std::abs((300.0 - px.y()) - 100.0) < 1e-6);  // 距底边 100 像素
  }

  void rejectsIncompleteYaml() {
    QTemporaryDir dir;
    QFile yaml(dir.filePath(QStringLiteral("bad.yaml")));
    QVERIFY(yaml.open(QIODevice::WriteOnly | QIODevice::Text));
    yaml.write("image: missing.pgm\n");
    yaml.close();
    BaseMapInfo info;
    QString err;
    QVERIFY(!loadBaseMap(yaml.fileName(), &info, &err));
    QVERIFY(!err.isEmpty());
  }
};

QTEST_MAIN(TestBaseMap)
#include "test_base_map.moc"
