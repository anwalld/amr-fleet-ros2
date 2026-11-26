#pragma once
// 界面与模型层唯一的数据入口。两种实现：ReplaySource（读回放文件）和 RosBridge（robot_console 包，连真实系统）。
// 信号可以从任意线程发出：接收方在界面线程时，Qt 自动用排队方式投递。
#include <QObject>
#include <QString>

#include <atomic>

#include "robot_console_core/types.hpp"

namespace robot_console {

class DataSource : public QObject {
  Q_OBJECT

 public:
  explicit DataSource(QObject* parent = nullptr) : QObject(parent) {}
  ~DataSource() override = default;

  // 状态栏显示的数据源名称（回放文件名或 ROS）。
  virtual QString description() const = 0;
  virtual void start() = 0;
  virtual void stop() {}

  quint64 serviceTimeouts() const { return service_timeouts_.load(); }
  quint64 skippedLines() const { return skipped_lines_.load(); }

 signals:
  void graphReceived(const robot_console::StationGraph& graph);
  void poseReceived(const robot_console::PoseSample& pose);
  void trafficReceived(const robot_console::TrafficStateData& traffic);
  void snapshotReceived(const robot_console::OperatorSnapshotData& snapshot);
  void eventReceived(const robot_console::FleetEvent& event);
  void alarmReceived(const QString& source, const QString& state, const QString& message);
  void fleetStopReceived(bool stopped);
  void connectionChanged(const robot_console::ConnectionStatus& status);

 protected:
  std::atomic<quint64> service_timeouts_{0};
  std::atomic<quint64> skipped_lines_{0};
};

}  // namespace robot_console
