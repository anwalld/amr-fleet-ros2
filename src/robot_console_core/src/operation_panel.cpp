#include "operation_panel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <limits>

#include "robot_console_core/resource_id.hpp"

namespace robot_console {
namespace {

QString operationName(Operation operation) {
  switch (operation) {
    case Operation::ListWorkflows: return QStringLiteral("工作流目录");
    case Operation::SubmitWorkflow: return QStringLiteral("工作流下单");
    case Operation::WorkflowStatus: return QStringLiteral("工作流查询");
    case Operation::PauseWorkflow: return QStringLiteral("暂停工作流");
    case Operation::ResumeWorkflow: return QStringLiteral("恢复工作流");
    case Operation::CancelWorkflow: return QStringLiteral("取消工作流");
    case Operation::SubmitTransport: return QStringLiteral("车队搬运");
    case Operation::BlockRoute: return QStringLiteral("封路");
    case Operation::UnblockRoute: return QStringLiteral("解封");
    case Operation::SetRobotEnabled: return QStringLiteral("更新调度资格");
    case Operation::PreviewClear: return QStringLiteral("预览清占用");
    case Operation::ClearReservation: return QStringLiteral("清占用");
    case Operation::SetFleetSimStop: return QStringLiteral("仿真车队急停");
  }
  return {};
}

bool workflowWrite(Operation operation) {
  return operation == Operation::SubmitWorkflow || operation == Operation::PauseWorkflow ||
         operation == Operation::ResumeWorkflow || operation == Operation::CancelWorkflow;
}

bool workflowControl(Operation operation) {
  return workflowWrite(operation) && operation != Operation::SubmitWorkflow;
}

bool routeWrite(Operation operation) {
  return operation == Operation::BlockRoute || operation == Operation::UnblockRoute;
}

void updateChoices(QComboBox* combo, const QStringList& ids) {
  if (combo->count() == ids.size()) {
    bool same = true;
    for (int i = 0; i < ids.size(); ++i) {
      same &= combo->itemText(i) == ids[i];
    }
    if (same) return;
  }
  const auto previous = combo->currentText();
  const QSignalBlocker blocked(combo);
  combo->clear();
  combo->addItems(ids);
  combo->setCurrentIndex(combo->findText(previous));
}

QString yesNo(bool value) { return value ? QStringLiteral("是") : QStringLiteral("否"); }

QString newId(const QString& prefix) {
  return prefix + QUuid::createUuid().toString(QUuid::Id128);
}

QSpinBox* priorityInput() {
  auto* input = new QSpinBox;
  input->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
  return input;
}

}  // namespace

OperationPanel::OperationPanel(OperationSource* source, QWidget* parent)
    : QWidget(parent), source_(source) {
  qRegisterMetaType<OperationResult>();
  auto* layout = new QVBoxLayout(this);
  sections_ = new QTabWidget(this);
  sections_->setObjectName(QStringLiteral("operationSections"));
  layout->addWidget(sections_);
  auto addForm = [this](const QString& name) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    auto* pane = new QWidget;
    auto* form = new QFormLayout(pane);
    area->setWidget(pane);
    sections_->addTab(area, name);
    return form;
  };

  auto* workflow = addForm(QStringLiteral("工作流"));
  workflow_choices_ = new QComboBox;
  workflow_choices_->setObjectName(QStringLiteral("workflowChoice"));
  workflow_priority_ = priorityInput();
  workflow_start_ = new QCheckBox(QStringLiteral("空闲即启动"));
  workflow_start_->setObjectName(QStringLiteral("workflowStart"));
  workflow_start_->setChecked(true);
  workflow_preempt_ = new QCheckBox(QStringLiteral("抢占当前任务"));
  workflow->addRow(button(QStringLiteral("加载目录"), "loadWorkflows", Operation::ListWorkflows));
  workflow->addRow(QStringLiteral("场景 / 工作流"), workflow_choices_);
  workflow->addRow(QStringLiteral("优先级"), workflow_priority_);
  workflow->addRow(workflow_start_);
  workflow->addRow(workflow_preempt_);
  workflow->addRow(button(QStringLiteral("提交工作流"), "submitWorkflow", Operation::SubmitWorkflow));
  order_id_ = new QLineEdit;
  order_id_->setObjectName(QStringLiteral("workflowOrderId"));
  order_id_->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("[A-Za-z0-9_-]+")), order_id_));
  order_id_->setPlaceholderText(QStringLiteral("输入工作流订单号"));
  workflow->addRow(QStringLiteral("订单号"), order_id_);
  workflow->addRow(button(QStringLiteral("查询"), "queryWorkflow", Operation::WorkflowStatus));
  include_active_ = new QCheckBox(QStringLiteral("包含运行中子任务"));
  include_active_->setObjectName(QStringLiteral("includeActive"));
  workflow->addRow(include_active_);
  auto* controls = new QWidget;
  auto* control_layout = new QHBoxLayout(controls);
  control_layout->setContentsMargins(0, 0, 0, 0);
  control_layout->addWidget(button(QStringLiteral("暂停"), "pauseWorkflow", Operation::PauseWorkflow));
  control_layout->addWidget(button(QStringLiteral("恢复"), "resumeWorkflow", Operation::ResumeWorkflow));
  control_layout->addWidget(button(QStringLiteral("取消"), "cancelWorkflow", Operation::CancelWorkflow));
  workflow->addRow(controls);
  workflow_status_ = new QLabel;
  workflow_status_->setObjectName(QStringLiteral("workflowStatus"));
  workflow_status_->setWordWrap(true);
  workflow_status_->setTextFormat(Qt::PlainText);
  workflow->addRow(workflow_status_);

  auto* transport = addForm(QStringLiteral("搬运"));
  pickup_ = new QComboBox;
  pickup_->setObjectName(QStringLiteral("pickupStation"));
  dropoff_ = new QComboBox;
  dropoff_->setObjectName(QStringLiteral("dropoffStation"));
  transport_priority_ = priorityInput();
  transport_start_ = new QCheckBox(QStringLiteral("空闲即启动"));
  transport_start_->setChecked(true);
  transport_preempt_ = new QCheckBox(QStringLiteral("抢占当前任务"));
  transport->addRow(QStringLiteral("取货站点"), pickup_);
  transport->addRow(QStringLiteral("送货站点"), dropoff_);
  transport->addRow(QStringLiteral("优先级"), transport_priority_);
  transport->addRow(transport_start_);
  transport->addRow(transport_preempt_);
  transport->addRow(button(QStringLiteral("提交搬运"), "submitTransport", Operation::SubmitTransport));

  auto* traffic = addForm(QStringLiteral("交通与车队"));
  route_ = new QComboBox;
  route_->setObjectName(QStringLiteral("routeTarget"));
  block_reason_ = new QLineEdit;
  block_reason_->setObjectName(QStringLiteral("blockReason"));
  traffic->addRow(QStringLiteral("路段"), route_);
  traffic->addRow(QStringLiteral("封路原因"), block_reason_);
  traffic->addRow(button(QStringLiteral("封路"), "blockRoute", Operation::BlockRoute));
  traffic->addRow(button(QStringLiteral("解封"), "unblockRoute", Operation::UnblockRoute));
  robot_ = new QComboBox;
  robot_->setObjectName(QStringLiteral("robotTarget"));
  enabled_ = new QCheckBox(QStringLiteral("允许调度"));
  enabled_->setObjectName(QStringLiteral("robotEnabled"));
  traffic->addRow(QStringLiteral("车辆"), robot_);
  traffic->addRow(enabled_);
  traffic->addRow(button(QStringLiteral("更新调度资格"), "setRobotEnabled", Operation::SetRobotEnabled));

  auto* maintenance = addForm(QStringLiteral("运维"));
  resource_ = new QComboBox;
  resource_->setObjectName(QStringLiteral("clearResource"));
  maintenance->addRow(QStringLiteral("占用资源"), resource_);
  maintenance->addRow(button(QStringLiteral("预览清理影响"), "previewClear", Operation::PreviewClear));
  preview_status_ = new QLabel;
  preview_status_->setWordWrap(true);
  preview_status_->setTextFormat(Qt::PlainText);
  preview_status_->setObjectName(QStringLiteral("clearPreview"));
  maintenance->addRow(preview_status_);
  maintenance->addRow(button(QStringLiteral("确认清理"), "confirmClear", Operation::ClearReservation));

  auto* safety = addForm(QStringLiteral("仿真急停"));
  fleet_stop_status_ = new QLabel;
  fleet_stop_status_->setObjectName(QStringLiteral("fleetStopStatus"));
  stop_fleet_ = new QPushButton(QStringLiteral("停止仿真车队"));
  stop_fleet_->setObjectName(QStringLiteral("stopFleetSim"));
  release_fleet_ = new QPushButton(QStringLiteral("解除仿真车队停止"));
  release_fleet_->setObjectName(QStringLiteral("releaseFleetSim"));
  safety->addRow(fleet_stop_status_);
  safety->addRow(stop_fleet_);
  safety->addRow(release_fleet_);
  const auto stop_target = QStringLiteral("仿真车队");
  auto command_stop = [this, stop_target](bool stop) {
    OperationRequest request;
    request.operation = Operation::SetFleetSimStop;
    request.target = stop_target;
    request.enabled = stop;
    send(request);
  };
  connect(stop_fleet_, &QPushButton::clicked, this, [command_stop] { command_stop(true); });
  connect(release_fleet_, &QPushButton::clicked, this, [this, command_stop] {
    const auto confirmed = QMessageBox::question(
        this, QStringLiteral("解除仿真车队停止"),
        QStringLiteral("解除后，仿真车辆可继续执行当前任务。确认解除？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirmed == QMessageBox::Yes && release_fleet_->isEnabled()) command_stop(false);
  });
  fleet_stop_ttl_.setSingleShot(true);
  fleet_stop_ttl_.setInterval(2000);
  fleet_stop_ttl_.setTimerType(Qt::PreciseTimer);
  connect(&fleet_stop_ttl_, &QTimer::timeout, this, &OperationPanel::refreshAvailability);

  availability_ = new QLabel;
  availability_->setObjectName(QStringLiteral("operationAvailability"));
  availability_->setWordWrap(true);
  layout->addWidget(availability_);
  history_ = new QTableWidget(0, 4, this);
  history_->setObjectName(QStringLiteral("operationHistory"));
  history_->setHorizontalHeaderLabels({QStringLiteral("操作"), QStringLiteral("目标"),
                                       QStringLiteral("服务结果"), QStringLiteral("状态观测")});
  history_->horizontalHeader()->setStretchLastSection(true);
  history_->verticalHeader()->hide();
  history_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(history_);

  connect(source_, &OperationSource::availabilityChanged, this, &OperationPanel::refreshAvailability,
          Qt::QueuedConnection);
  connect(source_, &OperationSource::operationCompleted, this, &OperationPanel::finish, Qt::QueuedConnection);
  connect(order_id_, &QLineEdit::textChanged, this, [this] {
    confirmed_workflow_.clear();
    confirmed_workflow_missions_.clear();
    workflow_status_->clear();
    refreshAvailability();
  });
  connect(workflow_choices_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (i >= 0 && i < workflows_.size()) workflow_priority_->setValue(workflows_[i].priority);
    refreshAvailability();
  });
  for (auto* selection : {pickup_, dropoff_, route_, robot_}) {
    connect(selection, &QComboBox::currentIndexChanged, this, &OperationPanel::refreshAvailability);
  }
  connect(robot_, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (i >= 0 && i < snapshot_.fleet_enabled.size()) enabled_->setChecked(snapshot_.fleet_enabled[i]);
  });
  connect(resource_, &QComboBox::currentTextChanged, this, [this](const QString& resource) {
    clear_resource_ = resource;
    clear_preview_ = {};
    clear_preview_request_ = 0;
    preview_status_->clear();
    refreshAvailability();
  });
  refreshAvailability();
}

QPushButton* OperationPanel::button(const QString& text, const char* name, Operation operation) {
  auto* control = new QPushButton(text);
  control->setObjectName(QString::fromLatin1(name));
  buttons_.insert(operation, control);
  connect(control, &QPushButton::clicked, this, [this, operation] {
    OperationRequest request;
    request.operation = operation;
    request.target = targetFor(operation);
    if (operation == Operation::SubmitWorkflow) {
      const auto& choice = workflows_.at(workflow_choices_->currentIndex());
      request.target = newId(QStringLiteral("ops_workflow_"));
      request.scenario_id = choice.scenario_id;
      request.workflow_id = choice.workflow_id;
      request.priority = workflow_priority_->value();
      request.start_if_idle = workflow_start_->isChecked();
      request.preempt_current = workflow_preempt_->isChecked();
      order_id_->setText(request.target);
    } else if (operation == Operation::SubmitTransport) {
      request.target = newId(QStringLiteral("ops_transport_"));
      request.from_station = pickup_->currentText();
      request.to_station = dropoff_->currentText();
      request.priority = transport_priority_->value();
      request.start_if_idle = transport_start_->isChecked();
      request.preempt_current = transport_preempt_->isChecked();
    } else if (workflowControl(operation)) {
      request.include_active = include_active_->isChecked();
      if (operation == Operation::CancelWorkflow &&
          QMessageBox::question(this, QStringLiteral("确认取消工作流"),
              QStringLiteral("订单 %1\n包含运行中子任务：%2")
                  .arg(request.target, yesNo(request.include_active)),
                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
      }
      if (request.target != order_id_->text().trimmed() || request.target != confirmed_workflow_) return;
    } else if (routeWrite(operation)) {
      const auto endpoints = route_endpoints_.value(request.target);
      request.from_station = endpoints.first;
      request.to_station = endpoints.second;
      request.reason = block_reason_->text().trimmed();
    } else if (operation == Operation::SetRobotEnabled) {
      request.enabled = enabled_->isChecked();
    } else if (operation == Operation::ClearReservation) {
      request.preview = clear_preview_;
    }
    send(std::move(request));
  });
  return control;
}

QString OperationPanel::targetFor(Operation operation) const {
  if (workflowControl(operation) || operation == Operation::WorkflowStatus) {
    return order_id_->hasAcceptableInput() ? order_id_->text() : QString();
  }
  if (routeWrite(operation)) return route_->currentText();
  if (operation == Operation::SetRobotEnabled) return robot_->currentText();
  if (operation == Operation::PreviewClear || operation == Operation::ClearReservation) return clear_resource_;
  return {};
}

bool OperationPanel::busy(Operation operation, const QString& target) const {
  for (const auto& entry : entries_) {
    if (!entry.pending) continue;
    const auto pending = entry.request.operation;
    if ((operation == pending && (entry.request.target == target || target.isEmpty())) ||
        (entry.request.target == target &&
         ((workflowWrite(operation) && workflowWrite(pending)) ||
          (routeWrite(operation) && routeWrite(pending)) ||
          (operation == Operation::PreviewClear && pending == Operation::ClearReservation) ||
          (operation == Operation::ClearReservation && pending == Operation::PreviewClear)))) {
      return true;
    }
  }
  return false;
}

void OperationPanel::refreshAvailability() {
  const bool can_stop = source_->available(Operation::SetFleetSimStop);
  const bool stop_busy = busy(Operation::SetFleetSimStop, QStringLiteral("仿真车队"));
  const bool fresh = fleet_stop_ttl_.isActive();
  stop_fleet_->setEnabled(can_stop && !stop_busy);
  release_fleet_->setEnabled(can_stop && !stop_busy && fresh && fleet_stopped_);
  fleet_stop_status_->setText(!fresh ? QStringLiteral("仿真控制状态未知") :
      fleet_stopped_ ? QStringLiteral("仿真停止已设置") : QStringLiteral("仿真停止已解除"));
  const bool can_clear = source_->available(Operation::PreviewClear) &&
                         source_->available(Operation::ClearReservation);
  availability_->setText(can_clear ? QString() : QStringLiteral("清占用不可用：需要预览和条件清理服务"));
  availability_->setVisible(!can_clear);
  for (auto it = buttons_.cbegin(); it != buttons_.cend(); ++it) {
    const auto operation = it.key();
    const auto target = targetFor(operation);
    bool enabled = source_->available(operation) && !busy(operation, target);
    if (operation == Operation::SubmitWorkflow) {
      enabled &= workflow_choices_->currentIndex() >= 0;
    } else if (operation == Operation::SubmitTransport) {
      enabled &= pickup_->currentIndex() >= 0 && dropoff_->currentIndex() >= 0 &&
                 pickup_->currentText() != dropoff_->currentText();
    } else if (operation != Operation::ListWorkflows) {
      enabled &= !target.isEmpty();
    }
    if (workflowControl(operation)) enabled &= target == confirmed_workflow_;
    if (operation == Operation::PreviewClear) enabled &= can_clear;
    if (operation == Operation::ClearReservation) {
      enabled &= can_clear && clear_preview_.found && traffic_.resource_ids.contains(target);
    }
    it.value()->setEnabled(enabled);
  }
}

void OperationPanel::send(OperationRequest request, quint64 related_operation) {
  static std::atomic<quint64> next_id{1};
  if (busy(request.operation, request.target)) return;
  request.id = next_id++;
  const int row = history_->rowCount();
  history_->insertRow(row);
  for (int column = 0; column < history_->columnCount(); ++column) {
    history_->setItem(row, column, new QTableWidgetItem);
  }
  history_->item(row, 0)->setText(request.operation == Operation::SetFleetSimStop && !request.enabled
      ? QStringLiteral("解除仿真车队停止") : operationName(request.operation));
  history_->item(row, 1)->setText(request.target);
  if (request.operation == Operation::PreviewClear) {
    clear_preview_request_ = request.id;
    clear_preview_ = {};
    preview_status_->setText(QStringLiteral("正在查询 %1").arg(request.target));
  } else if (request.operation == Operation::WorkflowStatus) {
    workflow_status_request_ = request.id;
  }
  entries_.insert(request.id, Entry{
      request, {}, true, row, snapshot_revision_, traffic_revision_, fleet_stop_revision_, related_operation, {}});
  render(entries_[request.id]);
  history_->scrollToBottom();
  if (!source_->submit(request)) {
    OperationResult result;
    result.id = request.id;
    result.message = QStringLiteral("服务不可用");
    finish(result);
  }
  refreshAvailability();
}

void OperationPanel::finish(const OperationResult& result) {
  auto it = entries_.find(result.id);
  if (it == entries_.end() || !it->pending) return;
  it->pending = false;
  it->result = result;
  const auto request = it->request;
  const auto related_operation = it->related_operation;
  render(*it);

  if (related_operation && result.outcome == OperationOutcome::Accepted &&
      !result.workflow_state.isEmpty() && result.workflow_state != QStringLiteral("UNKNOWN")) {
    auto original = entries_.find(related_operation);
    if (original != entries_.end()) {
      original->workflow_observation = QStringLiteral("%1：%2，暂停：%3")
          .arg(request.target, result.workflow_state, yesNo(result.paused));
      render(*original);
    }
  }

  if (request.operation == Operation::ListWorkflows && result.outcome == OperationOutcome::Accepted) {
    workflow_choices_->clear();
    workflows_ = result.workflows;
    for (const auto& workflow : workflows_) {
      workflow_choices_->addItem(QStringLiteral("%1 / %2  %3")
          .arg(workflow.scenario_id, workflow.workflow_id, workflow.label));
    }
  }
  if (request.operation == Operation::WorkflowStatus && result.id == workflow_status_request_ &&
      request.target == order_id_->text().trimmed()) {
    const bool known = result.outcome == OperationOutcome::Accepted && !result.workflow_state.isEmpty() &&
                       result.workflow_state != QStringLiteral("UNKNOWN");
    confirmed_workflow_missions_ = result.mission_ids;
    if (!result.running_mission_id.isEmpty()) confirmed_workflow_missions_.append(result.running_mission_id);
    confirmed_workflow_ = known && !confirmed_workflow_missions_.isEmpty() ? request.target : QString();
    workflow_status_->setText(QStringLiteral("%1：%2%3")
        .arg(request.target, result.workflow_state.isEmpty() ? result.message : result.workflow_state,
             known ? QStringLiteral("，暂停：%1").arg(yesNo(result.paused)) : QString()));
  }
  if (workflowWrite(request.operation) &&
      (result.outcome == OperationOutcome::Accepted || result.outcome == OperationOutcome::Unknown)) {
    OperationRequest query;
    query.operation = Operation::WorkflowStatus;
    query.target = request.target;
    send(std::move(query), result.id);
  }

  if (request.operation == Operation::PreviewClear && result.id == clear_preview_request_ &&
      request.target == clear_resource_) {
    clear_preview_ = result.preview;
    const auto message = clear_preview_.found
        ? QStringLiteral("占用者：%1\n将释放：%2\n将解除暂停：%3")
            .arg(clear_preview_.owner_id, clear_preview_.resource_ids.join(QStringLiteral("、")),
                 clear_preview_.resume_robot_ids.join(QStringLiteral("、")))
        : result.message;
    preview_status_->setText(message);
  }
  if (request.operation == Operation::ClearReservation && request.target == clear_resource_) {
    clear_preview_ = {};
    preview_status_->setText(result.message);
    if (result.outcome == OperationOutcome::Conflict) {
      OperationRequest preview;
      preview.operation = Operation::PreviewClear;
      preview.target = request.target;
      if (source_->available(Operation::ClearReservation)) send(std::move(preview));
    }
  }
  refreshAvailability();
}

void OperationPanel::render(const Entry& entry) {
  QString receipt;
  if (entry.pending) {
    receipt = QStringLiteral("提交中");
  } else {
    switch (entry.result.outcome) {
      case OperationOutcome::Accepted: receipt = QStringLiteral("受理"); break;
      case OperationOutcome::Rejected: receipt = QStringLiteral("拒绝"); break;
      case OperationOutcome::Unknown: receipt = QStringLiteral("结果未知"); break;
      case OperationOutcome::Conflict: receipt = QStringLiteral("冲突，需重新确认"); break;
    }
    if (!entry.result.message.isEmpty()) receipt += QStringLiteral("：") + entry.result.message;
    if (!entry.result.mission_ids.isEmpty()) {
      receipt += QStringLiteral(" · 子任务 ") + entry.result.mission_ids.join(QStringLiteral("、"));
    }
    if (workflowControl(entry.request.operation)) {
      receipt += QStringLiteral(" · 包含运行中：%1 · 已向运行中任务发出请求：%2")
          .arg(yesNo(entry.request.include_active), yesNo(entry.result.active_requested));
    }
    if (!entry.result.assigned_robot_id.isEmpty()) {
      receipt += QStringLiteral(" · 分配车辆 ") + entry.result.assigned_robot_id;
    }
    if (!entry.result.mission_id.isEmpty()) {
      receipt += QStringLiteral(" · 任务中枢编号 ") + entry.result.mission_id;
    }
    if (!entry.result.workflow_state.isEmpty()) {
      receipt += QStringLiteral(" · ") + entry.result.workflow_state;
    }
  }
  history_->item(entry.row, 2)->setText(receipt);
  history_->item(entry.row, 2)->setToolTip(receipt);
  const auto observed = observation(entry);
  if (!observed.isEmpty() || (!entry.pending &&
      (entry.result.outcome == OperationOutcome::Rejected || entry.result.outcome == OperationOutcome::Conflict))) {
    history_->item(entry.row, 3)->setText(observed);
    history_->item(entry.row, 3)->setToolTip(observed);
  }
}

QString OperationPanel::missionObservation(const QString& mission) const {
  if (mission.isEmpty()) return {};
  if (snapshot_.active_mission_id == mission && snapshot_.mission_active) {
    return QStringLiteral("%1 执行中").arg(mission);
  }
  if (snapshot_.queued_mission_ids.contains(mission)) return QStringLiteral("%1 排队中").arg(mission);
  const int latest = snapshot_.recent_event_mission_ids.indexOf(mission);
  if (latest >= 0 && latest < snapshot_.recent_event_states.size()) {
    return QStringLiteral("%1 %2").arg(mission, snapshot_.recent_event_states[latest]);
  }
  return {};
}

QString OperationPanel::observation(const Entry& entry) const {
  const auto& request = entry.request;
  if (!entry.pending && (entry.result.outcome == OperationOutcome::Rejected ||
                         entry.result.outcome == OperationOutcome::Conflict)) return {};
  const bool new_snapshot = snapshot_revision_ > entry.snapshot_before;
  const bool new_traffic = traffic_revision_ > entry.traffic_before;
  if (request.operation == Operation::SubmitTransport && new_snapshot) {
    const QString remote_mission = QStringLiteral("remote_fleet_task_") + request.target;
    if (!entry.result.mission_id.isEmpty() && entry.result.mission_id != remote_mission) {
      return missionObservation(entry.result.mission_id);
    }
    const int i = snapshot_.remote_task_ids.indexOf(request.target);
    if (i >= 0 && i < snapshot_.remote_task_states.size()) {
      return QStringLiteral("%1 %2 %3").arg(request.target,
          snapshot_.remote_task_robot_ids.value(i), snapshot_.remote_task_states[i]);
    }
  }
  if (request.operation == Operation::SubmitWorkflow && new_snapshot) {
    for (const auto& mission : entry.result.mission_ids) {
      const auto status = missionObservation(mission);
      if (!status.isEmpty()) return status;
    }
  }
  if (!entry.workflow_observation.isEmpty()) return entry.workflow_observation;
  if (request.operation == Operation::SetRobotEnabled && new_snapshot) {
    const int i = snapshot_.fleet_robot_ids.indexOf(request.target);
    if (i >= 0 && i < snapshot_.fleet_enabled.size() && snapshot_.fleet_enabled[i] == request.enabled) {
      return QStringLiteral("%1 已%2").arg(request.target,
          request.enabled ? QStringLiteral("启用") : QStringLiteral("停用"));
    }
  }
  if (routeWrite(request.operation) && new_traffic) {
    const int i = traffic_.resource_ids.indexOf(request.target);
    const bool blocked = i >= 0 && isBlockOwner(traffic_.owner_ids.value(i));
    if (blocked == (request.operation == Operation::BlockRoute)) {
      return blocked ? QStringLiteral("已观测封路") : QStringLiteral("已观测解封");
    }
  }
  if (request.operation == Operation::SetFleetSimStop && fleet_stop_revision_ > entry.fleet_stop_before &&
      fleet_stop_ttl_.isActive() && fleet_stopped_ == request.enabled) {
    return QStringLiteral("已观测控制状态");
  }
  if (request.operation == Operation::ClearReservation && new_traffic) {
    for (const auto& resource : request.preview.resource_ids) {
      const int i = traffic_.resource_ids.indexOf(resource);
      if (i >= 0 && traffic_.owner_ids.value(i) == request.preview.owner_id) return {};
    }
    return QStringLiteral("已观测原占用释放");
  }
  return {};
}

void OperationPanel::setGraph(const StationGraph& graph) {
  updateChoices(pickup_, graph.station_ids);
  updateChoices(dropoff_, graph.station_ids);
  route_endpoints_.clear();
  for (int i = 0; i < std::min(graph.from_station.size(), graph.to_station.size()); ++i) {
    const auto& from = graph.from_station[i];
    const auto& to = graph.to_station[i];
    if (from.isEmpty() || to.isEmpty() || from == to) continue;
    const auto resource = routeResourceId(from, to);
    if (!route_endpoints_.contains(resource) || graph.enabled.value(i, true)) {
      route_endpoints_.insert(resource, {from, to});
    }
  }
  QStringList routes = route_endpoints_.keys();
  routes.sort();
  updateChoices(route_, routes);
  refreshAvailability();
}

void OperationPanel::setSnapshot(const OperatorSnapshotData& snapshot) {
  if (!snapshot.success) return;
  ++snapshot_revision_;
  snapshot_ = snapshot;
  if (!confirmed_workflow_.isEmpty() &&
      std::none_of(confirmed_workflow_missions_.cbegin(), confirmed_workflow_missions_.cend(),
                   [&](const QString& id) {
                     return snapshot_.queued_mission_ids.contains(id) ||
                            (snapshot_.mission_active && snapshot_.active_mission_id == id);
                   })) {
    confirmed_workflow_.clear();
    confirmed_workflow_missions_.clear();
    workflow_status_->setText(QStringLiteral("目标已不在活动任务或队列中，请重新查询"));
  }
  updateChoices(robot_, snapshot.fleet_robot_ids);
  for (const auto& entry : entries_) render(entry);
  refreshAvailability();
}

void OperationPanel::setFleetStop(bool stopped) {
  if (fleet_stop_ttl_.isActive() && fleet_stopped_ == stopped) {
    fleet_stop_ttl_.start();
    return;
  }
  fleet_stopped_ = stopped;
  ++fleet_stop_revision_;
  fleet_stop_ttl_.start();
  for (const auto& entry : entries_) {
    if (entry.request.operation == Operation::SetFleetSimStop) render(entry);
  }
  refreshAvailability();
}

void OperationPanel::setTraffic(const TrafficStateData& traffic) {
  ++traffic_revision_;
  traffic_ = traffic;
  auto resources = traffic.resource_ids;
  // 冲突后的预览继续锚定原资源，即便更新时该资源已经消失。
  if (!clear_resource_.isEmpty() && !resources.contains(clear_resource_)) {
    resources.append(clear_resource_);
  }
  resources.sort();
  updateChoices(resource_, resources);
  for (const auto& entry : entries_) render(entry);
  refreshAvailability();
}

void OperationPanel::selectRoute(const QString& resource) {
  route_->setCurrentIndex(route_->findText(resource));
  sections_->setCurrentIndex(2);
}

void OperationPanel::selectRobot(const QString& robot) {
  robot_->setCurrentIndex(robot_->findText(robot));
  sections_->setCurrentIndex(2);
}

}  // namespace robot_console
