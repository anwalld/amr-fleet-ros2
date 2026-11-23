#include <QtTest>

#include "robot_console_core/resource_id.hpp"

using namespace robot_console;

class TestResourceId : public QObject {
  Q_OBJECT

 private slots:
  void edge() {
    const ResourceRef r = parseResource(QStringLiteral("route_edge:jn_s__machining"));
    QCOMPARE(r.kind, ResourceRef::Kind::Edge);
    QCOMPARE(r.a, QStringLiteral("jn_s"));
    QCOMPARE(r.b, QStringLiteral("machining"));
  }

  void edgeOutOfOrder() {
    // 核心代码按字典序排，这里也接受顺序颠倒的写法，结果统一成小在前。
    const ResourceRef r = parseResource(QStringLiteral("route_edge:machining__jn_s"));
    QCOMPARE(r.kind, ResourceRef::Kind::Edge);
    QCOMPARE(r.a, QStringLiteral("jn_s"));
    QCOMPARE(r.b, QStringLiteral("machining"));
    QCOMPARE(edgeKey(QStringLiteral("machining"), QStringLiteral("jn_s")),
             edgeKey(QStringLiteral("jn_s"), QStringLiteral("machining")));
  }

  void node() {
    const ResourceRef r = parseResource(QStringLiteral("route_node:jn_n"));
    QCOMPARE(r.kind, ResourceRef::Kind::Node);
    QCOMPARE(r.a, QStringLiteral("jn_n"));
  }

  void invalid() {
    QCOMPARE(parseResource(QStringLiteral("traffic_block:door")).kind, ResourceRef::Kind::Invalid);
    QCOMPARE(parseResource(QStringLiteral("route_edge:onlyone")).kind, ResourceRef::Kind::Invalid);
    QCOMPARE(parseResource(QStringLiteral("route_edge:__b")).kind, ResourceRef::Kind::Invalid);
    QCOMPARE(parseResource(QStringLiteral("route_node:")).kind, ResourceRef::Kind::Invalid);
    QVERIFY(isBlockOwner(QStringLiteral("traffic_block:maintenance")));
    QVERIFY(!isBlockOwner(QStringLiteral("robot_2")));
  }
};

QTEST_GUILESS_MAIN(TestResourceId)
#include "test_resource_id.moc"
