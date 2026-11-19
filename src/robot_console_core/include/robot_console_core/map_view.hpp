#pragma once
// 地图视图：缩放、平移，自动装下全部站点；绘制后记位置延迟和帧（冒烟度量）。
#include <QGraphicsView>

namespace robot_console {

class MapScene;
class SmokeMetrics;

class MapView : public QGraphicsView {
  Q_OBJECT

 public:
  explicit MapView(MapScene* scene, QWidget* parent = nullptr);

  void setMetrics(SmokeMetrics* metrics) { metrics_ = metrics; }
  void fitToStations();
  quint64 frameCount() const { return frames_; }

  // 位置时间戳与系统时间相差超过这个秒数时不记延迟样本（例如 Gazebo 栈用了仿真时间）。
  static constexpr double kMaxStampSkewS = 10.0;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  MapScene* map_scene_;
  SmokeMetrics* metrics_ = nullptr;
  bool auto_fit_ = true;
  quint64 frames_ = 0;
};

}  // namespace robot_console
