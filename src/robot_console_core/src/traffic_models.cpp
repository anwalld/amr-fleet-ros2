#include "robot_console_core/traffic_models.hpp"

#include <QHash>

#include <algorithm>

namespace robot_console {

OccupancyModel::OccupancyModel(QObject* parent) : QAbstractTableModel(parent) {}

int OccupancyModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : rows_.size(); }
int OccupancyModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 2; }

QVariant OccupancyModel::data(const QModelIndex& index, int role) const {
  if (role != Qt::DisplayRole || !index.isValid() || index.row() >= rows_.size()) return {};
  return index.column() == 0 ? rows_[index.row()].first : rows_[index.row()].second;
}

QVariant OccupancyModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
  return section == 0 ? QStringLiteral("资源") : QStringLiteral("占用者");
}

void OccupancyModel::setTraffic(const TrafficStateData& t) {
  beginResetModel();
  rows_.clear();
  for (int i = 0; i < t.resource_ids.size(); ++i) rows_.push_back({t.resource_ids[i], t.owner_ids.value(i)});
  endResetModel();
}

WaitQueueModel::WaitQueueModel(QObject* parent) : QAbstractTableModel(parent) {}

int WaitQueueModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : rows_.size(); }
int WaitQueueModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 4; }

QVariant WaitQueueModel::data(const QModelIndex& index, int role) const {
  if (role != Qt::DisplayRole || !index.isValid() || index.row() >= rows_.size()) return {};
  const Row& r = rows_[index.row()];
  switch (index.column()) {
    case 0: return r.resource;
    case 1: return r.robot;
    case 2: return r.priority;
    case 3: return r.position;
    default: return {};
  }
}

QVariant WaitQueueModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
  switch (section) {
    case 0: return QStringLiteral("资源");
    case 1: return QStringLiteral("等待车");
    case 2: return QStringLiteral("优先级");
    case 3: return QStringLiteral("队内位置");
    default: return {};
  }
}

void WaitQueueModel::setTraffic(const TrafficStateData& t) {
  beginResetModel();
  rows_.clear();
  QHash<QString, int> first_seen;
  for (int i = 0; i < t.waiting_resource_ids.size(); ++i) {
    Row r;
    r.resource = t.waiting_resource_ids[i];
    r.robot = t.waiting_robot_ids.value(i);
    r.priority = t.waiting_priorities.value(i);
    r.position = t.waiting_positions.value(i);
    if (!first_seen.contains(r.resource)) first_seen.insert(r.resource, first_seen.size());
    rows_.push_back(r);
  }
  std::stable_sort(rows_.begin(), rows_.end(), [&](const Row& a, const Row& b) {
    const int ga = first_seen.value(a.resource);
    const int gb = first_seen.value(b.resource);
    if (ga != gb) return ga < gb;
    return a.position < b.position;
  });
  endResetModel();
}

}  // namespace robot_console
