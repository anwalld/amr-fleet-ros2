#include "robot_console_core/map_view.hpp"

#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QMouseEvent>
#include <QMetaMethod>
#include <QWheelEvent>

#include <cmath>

#include "robot_console_core/map_scene.hpp"
#include "robot_console_core/smoke_metrics.hpp"
#include "robot_console_core/types.hpp"

namespace robot_console {

MapView::MapView(MapScene* scene, QWidget* parent) : QGraphicsView(scene, parent), map_scene_(scene) {
  setRenderHint(QPainter::Antialiasing);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setMinimumSize(320, 240);
  connect(scene, &MapScene::graphChanged, this, [this] {
    if (auto_fit_) fitToStations();
  });
}

void MapView::fitToStations() {
  const QRectF r = map_scene_->stationsRect();
  if (r.isNull()) return;
  const double margin = std::max(1.0, 0.08 * std::max(r.width(), r.height()));
  fitInView(r.adjusted(-margin, -margin, margin, margin), Qt::KeepAspectRatio);
}

void MapView::paintEvent(QPaintEvent* event) {
  QElapsedTimer timer;
  timer.start();
  QGraphicsView::paintEvent(event);
  const double paint_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
  ++frames_;
  // 本帧画出的位置有更新的车辆，各记一个延迟样本：绘制时刻的系统时间减位置的 stamp。
  const QHash<QString, double> stamps = map_scene_->takePaintedStamps();
  if (!metrics_) return;
  const double now = systemNowSeconds();
  for (auto it = stamps.cbegin(); it != stamps.cend(); ++it) {
    const double d = now - it.value();
    if (std::abs(d) > kMaxStampSkewS) {
      metrics_->addLatencySkipped();
    } else {
      metrics_->addLatencyMs(d * 1000.0);
    }
  }
  metrics_->addFrame(paint_ms);
}

void MapView::resizeEvent(QResizeEvent* event) {
  QGraphicsView::resizeEvent(event);
  if (auto_fit_) fitToStations();
}

void MapView::wheelEvent(QWheelEvent* event) {
  auto_fit_ = false;
  const double factor = std::pow(1.0015, event->angleDelta().y());
  scale(factor, factor);
}

void MapView::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) {
    auto_fit_ = true;
    fitToStations();
    return;
  }
  if (event->button() == Qt::RightButton && isSignalConnected(
          QMetaMethod::fromSignal(&MapView::routeSelected))) {
    const QPoint position = event->position().toPoint();
    const auto under_cursor = items(QRect(position - QPoint(4, 4), QSize(9, 9)));
    for (auto* item : under_cursor) {
      const QString resource = item->data(MapScene::kRouteResourceData).toString();
      if (!resource.isEmpty()) {
        emit routeSelected(resource);
        event->accept();
        return;
      }
    }
  }
  auto_fit_ = false;
  QGraphicsView::mousePressEvent(event);
}

}  // namespace robot_console
