#pragma once
// 交通面板：占用表、等待队列、死锁信息。
#include <QWidget>

#include "robot_console_core/types.hpp"

class QFormLayout;
class QLabel;

namespace robot_console {

class OccupancyModel;
class WaitQueueModel;

class TrafficPanel : public QWidget {
  Q_OBJECT

 public:
  explicit TrafficPanel(QWidget* parent = nullptr);

  void setTraffic(const TrafficStateData& traffic);
  OccupancyModel* occupancyModel() const { return occupancy_; }
  WaitQueueModel* waitModel() const { return waits_; }

  // 死锁信息标签的文字，键为标签 objectName：currentRing、checkCount、deadlockCount、resolvedCount、
  // actionCounts、lastResolution、lastResolutionMs、detectionUs。
  QString fieldText(const QString& name) const;

  static QString formatResolutionMs(double ms);

 private:
  QLabel* addField(QFormLayout* form, const QString& title, const QString& name);

  OccupancyModel* occupancy_;
  WaitQueueModel* waits_;
};

}  // namespace robot_console
