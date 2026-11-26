#pragma once
// 主窗口：左边地图，右边车队 / 任务 / 交通三个页签，底部状态栏。回放程序和 robot_console 共用。
#include <QHash>
#include <QMainWindow>
#include <QQueue>
#include <QSet>
#include <QTimer>

#include "robot_console_core/types.hpp"

class QLabel;
class QListWidget;
class QTableView;

namespace robot_console {

class DataSource;
class FleetTableModel;
class MapScene;
class MapView;
class OperationPanel;
class OperationSource;
class TaskQueueModel;
class TrafficPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  // 快照超过这个秒数没更新就标为过期。
  static constexpr double kSnapshotStaleS = 3.0;

  explicit MainWindow(DataSource* source, MonotonicMsFn clock = {}, QWidget* parent = nullptr,
                      OperationSource* operations = nullptr);

  bool loadBaseMap(const QString& yaml_path, QString* error);

  MapScene* mapScene() const { return scene_; }
  MapView* mapView() const { return view_; }
  FleetTableModel* fleetModel() const { return fleet_; }
  TaskQueueModel* taskModel() const { return tasks_; }
  TrafficPanel* trafficPanel() const { return traffic_panel_; }

  // 刷新状态栏（定时器每 500 ms 调用一次）。
  void refreshStatus();
  // 状态栏各段文字，用 " | " 连接。
  QString statusText() const;

 private:
  void onSnapshot(const OperatorSnapshotData& snapshot);
  void recordEvent(const QString& identity, const QString& source, const QString& state,
                   const QString& message);

  DataSource* source_;
  MonotonicMsFn clock_;
  MapScene* scene_;
  MapView* view_;
  FleetTableModel* fleet_;
  TaskQueueModel* tasks_;
  TrafficPanel* traffic_panel_;
  QTableView* fleet_table_;
  OperationPanel* operation_panel_ = nullptr;
  QListWidget* event_list_;
  QQueue<QString> event_keys_;
  QSet<QString> seen_events_;
  QHash<QString, QPair<QString, QString>> alarm_states_;
  QLabel* source_label_;
  QLabel* connection_label_;
  QLabel* mission_label_;
  QLabel* queue_label_;
  QLabel* age_label_;
  qint64 last_snapshot_ms_ = -1;
  QTimer status_timer_;
};

}  // namespace robot_console
