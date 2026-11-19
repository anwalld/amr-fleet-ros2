#pragma once
// 任务队列表：执行中的任务、排队中的任务（按快照顺序，带优先级）、远程任务。
#include <QAbstractTableModel>
#include <QVector>

#include "robot_console_core/types.hpp"

namespace robot_console {

class TaskQueueModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column { kCategory, kTaskId, kState, kPriority, kRobot, kColumnCount };

  explicit TaskQueueModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

  void setSnapshot(const OperatorSnapshotData& snapshot);

 private:
  struct Row {
    QString category;
    QString task;
    QString state;
    QString priority;
    QString robot;
  };
  QVector<Row> rows_;
};

}  // namespace robot_console
