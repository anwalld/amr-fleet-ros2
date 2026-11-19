#include "robot_console_core/task_queue_model.hpp"

namespace robot_console {

TaskQueueModel::TaskQueueModel(QObject* parent) : QAbstractTableModel(parent) {}

int TaskQueueModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : rows_.size();
}

int TaskQueueModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kColumnCount;
}

QVariant TaskQueueModel::data(const QModelIndex& index, int role) const {
  if (role != Qt::DisplayRole || !index.isValid() || index.row() >= rows_.size()) return {};
  const Row& r = rows_[index.row()];
  switch (index.column()) {
    case kCategory: return r.category;
    case kTaskId: return r.task;
    case kState: return r.state;
    case kPriority: return r.priority;
    case kRobot: return r.robot;
    default: return {};
  }
}

QVariant TaskQueueModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
  switch (section) {
    case kCategory: return QStringLiteral("类别");
    case kTaskId: return QStringLiteral("任务号");
    case kState: return QStringLiteral("状态");
    case kPriority: return QStringLiteral("优先级");
    case kRobot: return QStringLiteral("车辆");
    default: return {};
  }
}

void TaskQueueModel::setSnapshot(const OperatorSnapshotData& s) {
  beginResetModel();
  rows_.clear();
  if (!s.active_mission_id.isEmpty()) {
    rows_.push_back({QStringLiteral("执行中"), s.active_mission_id, s.mission_state, QString(), QString()});
  }
  for (int i = 0; i < s.queued_mission_ids.size(); ++i) {
    const QString prio = i < s.queued_priorities.size() ? QString::number(s.queued_priorities[i]) : QString();
    rows_.push_back({QStringLiteral("排队"), s.queued_mission_ids[i],
                     QStringLiteral("排队第 %1 位").arg(i + 1), prio, QString()});
  }
  for (int i = 0; i < s.remote_task_ids.size(); ++i) {
    rows_.push_back({QStringLiteral("远程"), s.remote_task_ids[i], s.remote_task_states.value(i), QString(),
                     s.remote_task_robot_ids.value(i)});
  }
  endResetModel();
}

}  // namespace robot_console
