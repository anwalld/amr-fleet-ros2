#include "robot_console_core/fleet_table_model.hpp"

namespace robot_console {

FleetTableModel::FleetTableModel(QObject* parent) : QAbstractTableModel(parent) {}

int FleetTableModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : rows_.size();
}

int FleetTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : kColumnCount;
}

bool FleetTableModel::isTerminalTaskState(const QString& state) {
  return state == QLatin1String("FINISHED") || state == QLatin1String("FAILED") ||
         state == QLatin1String("CANCELED");
}

QVariant FleetTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rows_.size()) return {};
  const Row& r = rows_[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
      case kRobot: return r.robot;
      case kEnabled: return r.enabled ? QStringLiteral("是") : QStringLiteral("否");
      case kState: return r.state;
      case kVoltage:
        // 按快照里的伏特数显示，不换算成百分比。
        return r.has_voltage ? QStringLiteral("%1 V").arg(r.voltage, 0, 'f', 2) : QString();
      case kStation: return r.station;
      case kTask: return r.task;
      default: return {};
    }
  }
  if (role == Qt::UserRole && index.column() == kVoltage) return r.voltage;
  return {};
}

QVariant FleetTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
  switch (section) {
    case kRobot: return QStringLiteral("车辆号");
    case kEnabled: return QStringLiteral("启用");
    case kState: return QStringLiteral("状态");
    case kVoltage: return QStringLiteral("电池电压");
    case kStation: return QStringLiteral("当前站点");
    case kTask: return QStringLiteral("当前任务");
    default: return {};
  }
}

void FleetTableModel::setSnapshot(const OperatorSnapshotData& s) {
  beginResetModel();
  rows_.clear();
  for (int i = 0; i < s.fleet_robot_ids.size(); ++i) {
    Row r;
    r.robot = s.fleet_robot_ids[i];
    r.enabled = i < s.fleet_enabled.size() && s.fleet_enabled[i];
    r.state = s.fleet_states.value(i);
    r.has_voltage = i < s.fleet_battery_voltage.size();
    r.voltage = r.has_voltage ? s.fleet_battery_voltage[i] : 0.0;
    r.station = s.fleet_current_station_ids.value(i);
    QStringList tasks;
    for (int k = 0; k < s.remote_task_ids.size(); ++k) {
      if (s.remote_task_robot_ids.value(k) == r.robot &&
          !isTerminalTaskState(s.remote_task_states.value(k))) {
        tasks.push_back(s.remote_task_ids[k]);
      }
    }
    r.task = tasks.join(QStringLiteral(", "));
    rows_.push_back(r);
  }
  endResetModel();
}

}  // namespace robot_console
