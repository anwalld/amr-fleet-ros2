#pragma once
// 车队表：每辆车一行。列：车辆号、启用、状态、电池电压、当前站点、当前任务。
#include <QAbstractTableModel>
#include <QVector>

#include "robot_console_core/types.hpp"

namespace robot_console {

class FleetTableModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column { kRobot, kEnabled, kState, kVoltage, kStation, kTask, kColumnCount };

  explicit FleetTableModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

  void setSnapshot(const OperatorSnapshotData& snapshot);

  // 远程任务的终止状态。
  static bool isTerminalTaskState(const QString& state);

 private:
  struct Row {
    QString robot;
    bool enabled = false;
    QString state;
    double voltage = 0.0;
    bool has_voltage = false;
    QString station;
    QString task;
  };
  QVector<Row> rows_;
};

}  // namespace robot_console
