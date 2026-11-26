#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace robot_console {

enum class Operation {
  ListWorkflows, SubmitWorkflow, WorkflowStatus, PauseWorkflow, ResumeWorkflow, CancelWorkflow,
  SubmitTransport, BlockRoute, UnblockRoute, SetRobotEnabled, PreviewClear, ClearReservation, SetFleetSimStop
};

struct ClearPreview {
  bool found = false;
  QString owner_id;
  QStringList resource_ids;
  QStringList resume_robot_ids;
};

struct OperationRequest {
  quint64 id = 0;
  Operation operation = Operation::ListWorkflows;
  QString target;
  QString scenario_id;
  QString workflow_id;
  QString from_station;
  QString to_station;
  QString reason;
  int priority = 0;
  bool start_if_idle = true;
  bool preempt_current = false;
  bool include_active = false;
  bool enabled = true;
  ClearPreview preview;
};

enum class OperationOutcome { Accepted, Rejected, Unknown, Conflict };

struct WorkflowChoice {
  QString scenario_id;
  QString workflow_id;
  QString label;
  int priority = 0;
};

struct OperationResult {
  quint64 id = 0;
  OperationOutcome outcome = OperationOutcome::Rejected;
  QString message;
  QVector<WorkflowChoice> workflows;
  QStringList mission_ids;
  bool active_requested = false;
  QString running_mission_id;
  QStringList finished_mission_ids;
  QString workflow_state;
  bool paused = false;
  QString assigned_robot_id;
  QString mission_id;
  ClearPreview preview;
};

class OperationSource : public QObject {
  Q_OBJECT

 public:
  using QObject::QObject;
  virtual bool available(Operation operation) const = 0;
  virtual bool submit(const OperationRequest& request) = 0;

 signals:
  void availabilityChanged();
  void operationCompleted(const robot_console::OperationResult& result);
};

}  // namespace robot_console

Q_DECLARE_METATYPE(robot_console::OperationResult)
