#pragma once

#include <QHash>
#include <QTimer>
#include <QWidget>

#include "robot_console_core/operation_source.hpp"
#include "robot_console_core/types.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

namespace robot_console {

class OperationPanel final : public QWidget {
  Q_OBJECT

 public:
  explicit OperationPanel(OperationSource* source, QWidget* parent = nullptr);

  void setGraph(const StationGraph& graph);
  void setSnapshot(const OperatorSnapshotData& snapshot);
  void setTraffic(const TrafficStateData& traffic);
  void setFleetStop(bool stopped);
  void selectRoute(const QString& resource);
  void selectRobot(const QString& robot);

 private:
  struct Entry {
    OperationRequest request;
    OperationResult result;
    bool pending = true;
    int row = 0;
    quint64 snapshot_before = 0;
    quint64 traffic_before = 0;
    quint64 fleet_stop_before = 0;
    quint64 related_operation = 0;
    QString workflow_observation;
    QString remote_observation;
  };

  QPushButton* button(const QString& text, const char* name, Operation operation);
  void send(OperationRequest request, quint64 related_operation = 0);
  void finish(const OperationResult& result);
  void render(const Entry& entry);
  void refreshAvailability();
  bool busy(Operation operation, const QString& target) const;
  QString observation(const Entry& entry) const;
  QString missionObservation(const QString& mission) const;
  QString targetFor(Operation operation) const;

  void sendRemoteTask(RemoteTaskAction action);
  void finishRemoteTask(const OperationRequest& request, const OperationResult& result, quint64 original);
  void refreshRemoteTask();
  void reconcileRemoteTasks();
  QHash<QString, OperationResult> remote_tasks_;
  QHash<QString, quint64> remote_unresolved_;
  QHash<RemoteTaskAction, QPushButton*> remote_buttons_;
  QComboBox* remote_task_;
  QComboBox* remote_destination_;
  QLabel* remote_status_;
  QTimer remote_reconcile_timer_;

  OperationSource* source_;
  QHash<quint64, Entry> entries_;
  quint64 clear_preview_request_ = 0;
  quint64 workflow_status_request_ = 0;
  QString clear_resource_;
  ClearPreview clear_preview_;
  QString confirmed_workflow_;
  QStringList confirmed_workflow_missions_;
  quint64 snapshot_revision_ = 0;
  quint64 traffic_revision_ = 0;
  quint64 fleet_stop_revision_ = 0;
  bool fleet_stopped_ = true;
  QTimer fleet_stop_ttl_;
  OperatorSnapshotData snapshot_;
  TrafficStateData traffic_;
  QVector<WorkflowChoice> workflows_;
  QHash<Operation, QPushButton*> buttons_;

  QTabWidget* sections_;
  QComboBox* workflow_choices_;
  QSpinBox* workflow_priority_;
  QCheckBox* workflow_start_;
  QCheckBox* workflow_preempt_;
  QLineEdit* order_id_;
  QCheckBox* include_active_;
  QLabel* workflow_status_;
  QComboBox* pickup_;
  QComboBox* dropoff_;
  QSpinBox* transport_priority_;
  QCheckBox* transport_start_;
  QCheckBox* transport_preempt_;
  QComboBox* route_;
  QHash<QString, QPair<QString, QString>> route_endpoints_;
  QLineEdit* block_reason_;
  QComboBox* robot_;
  QCheckBox* enabled_;
  QComboBox* resource_;
  QLabel* preview_status_;
  QLabel* availability_;
  QLabel* fleet_stop_status_;
  QPushButton* stop_fleet_;
  QPushButton* release_fleet_;
  QTableWidget* history_;
};

}  // namespace robot_console
