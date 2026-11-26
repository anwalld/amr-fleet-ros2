#include "robot_console_core/map_scene.hpp"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QRegularExpression>

#include <cmath>

#include "robot_console_core/resource_id.hpp"

namespace robot_console {
namespace {

constexpr double kStationRadius = 0.25;  // 米
constexpr double kArrowLength = 0.5;     // 米
constexpr double kPi = 3.14159265358979323846;

enum ZOrder { kZBaseMap = -10, kZEdge = 0, kZStation = 1, kZWait = 2, kZRobot = 3 };

QPen cosmeticPen(const QColor& c, double width, Qt::PenStyle style = Qt::SolidLine) {
  QPen p(c, width, style, Qt::RoundCap, Qt::RoundJoin);
  p.setCosmetic(true);
  return p;
}

QColor stationColor() { return QColor(0x5f, 0x63, 0x68); }

}  // namespace

MapScene::MapScene(QObject* parent) : QGraphicsScene(parent) {
  setBackgroundBrush(QColor(0xfa, 0xfa, 0xf7));
  flush_timer_.setInterval(33);  // 合并刷新，帧率上限约 30
  connect(&flush_timer_, &QTimer::timeout, this, &MapScene::flushPending);
  flush_timer_.start();
}

QColor MapScene::robotColor(const QString& robot) {
  // Tableau 10。robot_N 取第 (N-1) mod 10 色，其他车辆号按哈希取色。
  static const QVector<QColor> palette = {
      QColor(0x4e, 0x79, 0xa7), QColor(0xf2, 0x8e, 0x2b), QColor(0xe1, 0x57, 0x59), QColor(0x76, 0xb7, 0xb2),
      QColor(0x59, 0xa1, 0x4f), QColor(0xed, 0xc9, 0x48), QColor(0xb0, 0x7a, 0xa1), QColor(0xff, 0x9d, 0xa7),
      QColor(0x9c, 0x75, 0x5f), QColor(0xba, 0xb0, 0xac)};
  static const QRegularExpression re(QStringLiteral("^robot_(\\d+)$"));
  const auto m = re.match(robot);
  int idx = 0;
  if (m.hasMatch()) {
    idx = (m.captured(1).toInt() - 1) % palette.size();
    if (idx < 0) idx += palette.size();
  } else {
    idx = static_cast<int>(qHash(robot) % static_cast<uint>(palette.size()));
  }
  return palette[idx];
}

QColor MapScene::defaultEdgeColor() { return QColor(0x9a, 0xa0, 0xa6); }
QColor MapScene::blockColor() { return QColor(0x55, 0x55, 0x55); }

void MapScene::clearGraph() {
  for (auto* item : std::as_const(stations_)) {
    removeItem(item);
    delete item;
  }
  stations_.clear();
  for (auto& e : edges_) {
    removeItem(e.line);
    delete e.line;  // 箭头是子图元，随之删除
  }
  edges_.clear();
}

void MapScene::setGraph(const StationGraph& g) {
  clearGraph();
  QFont label_font;
  label_font.setPointSize(8);
  for (int i = 0; i < g.station_ids.size(); ++i) {
    const QPointF p = toScene(g.x.value(i), g.y.value(i));
    auto* dot = addEllipse(-kStationRadius, -kStationRadius, 2 * kStationRadius, 2 * kStationRadius,
                           cosmeticPen(stationColor().darker(130), 1), QBrush(stationColor()));
    dot->setPos(p);
    dot->setZValue(kZStation);
    dot->setToolTip(g.station_ids[i]);
    auto* label = new QGraphicsSimpleTextItem(g.station_ids[i], dot);
    label->setFont(label_font);
    label->setBrush(QColor(0x33, 0x33, 0x33));
    label->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    label->setPos(kStationRadius, kStationRadius);
    stations_.insert(g.station_ids[i], dot);
  }
  for (int i = 0; i < g.from_station.size(); ++i) {
    const QString from = g.from_station[i];
    const QString to = g.to_station.value(i);
    if (!stations_.contains(from) || !stations_.contains(to) || from == to) continue;
    const QString key = edgeKey(from, to);
    Edge& e = edges_[key];
    if (e.a.isEmpty()) {
      e.a = std::min(from, to);
      e.b = std::max(from, to);
    }
    const bool fwd = from == e.a;
    const bool bidir = g.bidirectional.value(i, false);
    if (fwd || bidir) e.forward = true;
    if (!fwd || bidir) e.backward = true;
    if (!g.enabled.value(i, true)) e.enabled = false;
  }
  for (auto& e : edges_) {
    const QPointF pa = stations_.value(e.a)->pos();
    const QPointF pb = stations_.value(e.b)->pos();
    e.line = addLine(QLineF(pa, pb));
    e.line->setZValue(kZEdge);
    e.line->setData(kRouteResourceData, routeResourceId(e.a, e.b));
    e.line->setToolTip(QStringLiteral("%1 — %2").arg(e.a, e.b));
    if (e.forward != e.backward) {
      // 单向：在中点画箭头，指向行驶方向。
      const QPointF from = e.forward ? pa : pb;
      const QPointF to = e.forward ? pb : pa;
      const QPointF mid = (from + to) / 2.0;
      const double ang = std::atan2(to.y() - from.y(), to.x() - from.x());
      const QPointF tip = mid + QPointF(std::cos(ang), std::sin(ang)) * (kArrowLength / 2);
      const QPointF back = mid - QPointF(std::cos(ang), std::sin(ang)) * (kArrowLength / 2);
      const QPointF side(-std::sin(ang) * kArrowLength * 0.35, std::cos(ang) * kArrowLength * 0.35);
      e.arrow = new QGraphicsPolygonItem(QPolygonF({tip, back + side, back - side}), e.line);
      e.arrow->setPen(Qt::NoPen);
    }
  }
  applyTrafficStyles();
  updateWaitLines();
  emit graphChanged();
}

void MapScene::setBaseMap(const BaseMapInfo& info) {
  if (base_map_) {
    removeItem(base_map_);
    delete base_map_;
  }
  base_map_ = addPixmap(QPixmap::fromImage(info.image));
  base_map_->setZValue(kZBaseMap);
  base_map_->setScale(info.resolution);
  // 图像第 0 行在最上方，对应地图 y = origin_y + 高度 × 分辨率。
  base_map_->setPos(toScene(info.origin_x, info.origin_y + info.image.height() * info.resolution));
}

void MapScene::setPose(const PoseSample& pose) { pending_poses_.insert(pose.robot, pose); }

void MapScene::setTraffic(const TrafficStateData& traffic) {
  traffic_ = traffic;
  have_traffic_ = true;
  waits_.clear();
  for (int i = 0; i < traffic.waiting_resource_ids.size(); ++i) {
    waits_.push_back({traffic.waiting_robot_ids.value(i), traffic.waiting_resource_ids[i]});
  }
  const QSet<QString> old = deadlock_robots_;
  deadlock_robots_.clear();
  for (const QString& r : traffic.deadlock_robot_ids) deadlock_robots_.insert(r);
  for (const QString& r : old + deadlock_robots_) styleRobot(r);
  applyTrafficStyles();
  updateWaitLines();
}

void MapScene::applyTrafficStyles() {
  for (auto& e : edges_) {
    e.line->setPen(cosmeticPen(defaultEdgeColor(), 3, e.enabled ? Qt::SolidLine : Qt::DashLine));
    if (e.arrow) e.arrow->setBrush(defaultEdgeColor().darker(120));
  }
  for (auto* s : std::as_const(stations_)) s->setBrush(stationColor());
  if (!have_traffic_) return;
  quint64 invalid = 0;
  for (int i = 0; i < traffic_.resource_ids.size(); ++i) {
    const ResourceRef ref = parseResource(traffic_.resource_ids[i]);
    const QString owner = traffic_.owner_ids.value(i);
    const bool block = isBlockOwner(owner);
    const QColor color = block ? blockColor() : robotColor(owner);
    if (ref.kind == ResourceRef::Kind::Edge) {
      auto it = edges_.find(edgeKey(ref.a, ref.b));
      if (it == edges_.end()) continue;
      it->line->setPen(cosmeticPen(color, 5, block ? Qt::DashDotLine : Qt::SolidLine));
      if (it->arrow) it->arrow->setBrush(color.darker(130));
    } else if (ref.kind == ResourceRef::Kind::Node) {
      if (auto* s = stations_.value(ref.a)) s->setBrush(color);
    } else {
      ++invalid;
    }
  }
  invalid_resources_ += invalid;
}

QPointF MapScene::resourceScenePos(const QString& resource, bool* ok) const {
  const ResourceRef ref = parseResource(resource);
  *ok = false;
  if (ref.kind == ResourceRef::Kind::Edge) {
    auto* sa = stations_.value(ref.a);
    auto* sb = stations_.value(ref.b);
    if (!sa || !sb) return {};
    *ok = true;
    return (sa->pos() + sb->pos()) / 2.0;
  }
  if (ref.kind == ResourceRef::Kind::Node) {
    auto* s = stations_.value(ref.a);
    if (!s) return {};
    *ok = true;
    return s->pos();
  }
  return {};
}

void MapScene::updateWaitLines() {
  for (auto* l : std::as_const(wait_lines_)) {
    removeItem(l);
    delete l;
  }
  wait_lines_.clear();
  for (const Wait& w : waits_) {
    auto* robot = robots_.value(w.robot);
    if (!robot) continue;  // 还没收到过位置的车没法画
    bool ok = false;
    const QPointF target = resourceScenePos(w.resource, &ok);
    if (!ok) continue;
    auto* line = addLine(QLineF(robot->pos(), target), cosmeticPen(robotColor(w.robot), 2, Qt::DashLine));
    line->setZValue(kZWait);
    line->setToolTip(QStringLiteral("%1 在等 %2").arg(w.robot, w.resource));
    wait_lines_.push_back(line);
  }
}

void MapScene::styleRobot(const QString& robot) {
  auto* item = robots_.value(robot);
  if (!item) return;
  if (deadlock_robots_.contains(robot)) {
    item->setPen(cosmeticPen(QColor(0xd9, 0x1e, 0x18), 3));
  } else {
    item->setPen(cosmeticPen(QColor(0x20, 0x20, 0x20), 1));
  }
}

void MapScene::flushPending() {
  if (pending_poses_.isEmpty()) return;
  for (auto it = pending_poses_.cbegin(); it != pending_poses_.cend(); ++it) {
    const PoseSample& p = it.value();
    auto* item = robots_.value(p.robot);
    if (!item) {
      // 车头朝本地 +x 的三角形，像素大小固定。
      item = addPolygon(QPolygonF({QPointF(14, 0), QPointF(-8, 8), QPointF(-8, -8)}));
      item->setBrush(robotColor(p.robot));
      item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
      item->setZValue(kZRobot);
      auto* label = new QGraphicsSimpleTextItem(p.robot, item);
      QFont robot_font;
      robot_font.setPointSize(9);
      robot_font.setBold(true);
      label->setFont(robot_font);
      label->setPos(10, 6);
      robots_.insert(p.robot, item);
      styleRobot(p.robot);
    }
    item->setPos(toScene(p.x, p.y));
    // 场景 y 轴已翻转，所以地图上逆时针的 yaw 在场景里是负角度。
    item->setRotation(-p.yaw * 180.0 / kPi);
    // 标签不跟着车头转。
    for (auto* child : item->childItems()) child->setRotation(p.yaw * 180.0 / kPi);
    painted_stamps_.insert(p.robot, p.stamp);
  }
  pending_poses_.clear();
  updateWaitLines();
}

QHash<QString, double> MapScene::takePaintedStamps() {
  QHash<QString, double> out;
  out.swap(painted_stamps_);
  return out;
}

QRectF MapScene::stationsRect() const {
  QRectF r;
  for (auto* s : std::as_const(stations_)) {
    const QRectF one(s->pos() - QPointF(kStationRadius, kStationRadius),
                     QSizeF(2 * kStationRadius, 2 * kStationRadius));
    r = r.isNull() ? one : r.united(one);
  }
  return r;
}

QPointF MapScene::stationScenePos(const QString& station) const {
  auto* s = stations_.value(station);
  return s ? s->pos() : QPointF();
}

const MapScene::Edge* MapScene::edge(const QString& s1, const QString& s2) const {
  auto it = edges_.find(edgeKey(s1, s2));
  return it == edges_.end() ? nullptr : &it.value();
}

QGraphicsEllipseItem* MapScene::stationItem(const QString& station) const { return stations_.value(station); }

QGraphicsPolygonItem* MapScene::robotItem(const QString& robot) const { return robots_.value(robot); }

}  // namespace robot_console
