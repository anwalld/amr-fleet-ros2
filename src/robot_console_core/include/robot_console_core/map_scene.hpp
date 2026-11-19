#pragma once
// 地图场景：站点、路段、车辆、等待虚线，可选底图。
// 场景单位是米；地图坐标 (x, y) 放在场景 (x, -y)，屏幕上 y 轴朝上。
#include <QColor>
#include <QGraphicsScene>
#include <QHash>
#include <QPointF>
#include <QSet>
#include <QTimer>
#include <QVector>

#include "robot_console_core/base_map.hpp"
#include "robot_console_core/types.hpp"

class QGraphicsEllipseItem;
class QGraphicsLineItem;
class QGraphicsPixmapItem;
class QGraphicsPolygonItem;
class QGraphicsSimpleTextItem;

namespace robot_console {

class MapScene : public QGraphicsScene {
  Q_OBJECT

 public:
  struct Edge {
    QString a;  // 字典序较小
    QString b;
    bool forward = false;   // 有 a -> b 的路段
    bool backward = false;  // 有 b -> a 的路段
    bool enabled = true;    // 所有方向都启用
    QGraphicsLineItem* line = nullptr;
    QGraphicsPolygonItem* arrow = nullptr;  // 单向时的箭头，双向时为空
  };

  explicit MapScene(QObject* parent = nullptr);

  static QPointF toScene(double x, double y) { return {x, -y}; }
  static QColor robotColor(const QString& robot);
  static QColor defaultEdgeColor();
  static QColor blockColor();

  void setGraph(const StationGraph& graph);
  void setBaseMap(const BaseMapInfo& info);
  void setPose(const PoseSample& pose);           // 先记下，合并刷新时画
  void setTraffic(const TrafficStateData& traffic);
  void flushPending();                            // 33 ms 定时器调用；测试可直接调用

  // 自上次取走以来画到图元上的位置时间戳（车辆 -> stamp），地图视图绘制后取走。
  QHash<QString, double> takePaintedStamps();

  QRectF stationsRect() const;
  QPointF stationScenePos(const QString& station) const;

  // 测试和面板用的查询接口。
  const Edge* edge(const QString& s1, const QString& s2) const;
  QGraphicsEllipseItem* stationItem(const QString& station) const;
  QGraphicsPolygonItem* robotItem(const QString& robot) const;
  QGraphicsPixmapItem* baseMapItem() const { return base_map_; }
  int edgeCount() const { return edges_.size(); }
  int waitLineCount() const { return wait_lines_.size(); }
  QVector<QGraphicsLineItem*> waitLines() const { return wait_lines_; }
  bool isRobotHighlighted(const QString& robot) const { return deadlock_robots_.contains(robot); }
  quint64 invalidResourceCount() const { return invalid_resources_; }

 signals:
  void graphChanged();

 private:
  struct Wait {
    QString robot;
    QString resource;
  };

  void clearGraph();
  void applyTrafficStyles();
  void updateWaitLines();
  void styleRobot(const QString& robot);
  QPointF resourceScenePos(const QString& resource, bool* ok) const;

  QHash<QString, QGraphicsEllipseItem*> stations_;
  QHash<QString, Edge> edges_;  // edgeKey -> Edge
  QHash<QString, QGraphicsPolygonItem*> robots_;
  QHash<QString, PoseSample> pending_poses_;
  QHash<QString, double> painted_stamps_;
  QVector<Wait> waits_;
  QVector<QGraphicsLineItem*> wait_lines_;
  QSet<QString> deadlock_robots_;
  TrafficStateData traffic_;
  bool have_traffic_ = false;
  quint64 invalid_resources_ = 0;
  QGraphicsPixmapItem* base_map_ = nullptr;
  QTimer flush_timer_;
};

}  // namespace robot_console
