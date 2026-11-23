#pragma once
// 交通面板的两张表：占用表（资源、占用者）和等待队列（资源、等待车、优先级、队内位置）。
#include <QAbstractTableModel>
#include <QVector>

#include "robot_console_core/types.hpp"

namespace robot_console {

class OccupancyModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  explicit OccupancyModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  void setTraffic(const TrafficStateData& traffic);

 private:
  QVector<QPair<QString, QString>> rows_;
};

class WaitQueueModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  struct Row {
    QString resource;
    QString robot;
    int priority = 0;
    int position = 0;
  };

  explicit WaitQueueModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
  // 同一资源按队内位置排序；资源之间保持首次出现的顺序。
  void setTraffic(const TrafficStateData& traffic);
  const QVector<Row>& rows() const { return rows_; }

 private:
  QVector<Row> rows_;
};

}  // namespace robot_console
