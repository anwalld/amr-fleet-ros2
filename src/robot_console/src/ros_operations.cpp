#include "robot_console/ros_operations.hpp"

#include <robot_interfaces_business/srv/cancel_workflow.hpp>
#include <robot_interfaces_business/srv/get_workflow_status.hpp>
#include <robot_interfaces_business/srv/list_scenario_workflows.hpp>
#include <robot_interfaces_business/srv/pause_workflow.hpp>
#include <robot_interfaces_business/srv/resume_workflow.hpp>
#include <robot_interfaces_business/srv/submit_order.hpp>
#include <robot_interfaces_fleet/srv/control_remote_fleet_task.hpp>
#include <robot_interfaces_fleet/srv/submit_fleet_station_task.hpp>
#include <robot_interfaces_fleet/srv/update_fleet_robot_state.hpp>
#include <robot_interfaces_mission/srv/block_station_route.hpp>
#include <robot_interfaces_mission/srv/clear_traffic_reservation_if_matches.hpp>
#include <robot_interfaces_mission/srv/preview_traffic_reservation_clear.hpp>
#include <robot_interfaces_mission/srv/unblock_station_route.hpp>

#include <std_srvs/srv/set_bool.hpp>

#include <algorithm>
#include <chrono>

#include <QJsonDocument>
#include <QJsonObject>

namespace robot_console {
namespace {
namespace business = robot_interfaces_business::srv;
namespace fleet = robot_interfaces_fleet::srv;
namespace mission = robot_interfaces_mission::srv;
using namespace std::chrono_literals;

double nowSeconds() {
  return std::chrono::duration<double>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

QStringList strings(const std::vector<std::string>& values) {
  QStringList result;
  for (const auto& value : values) {
    result.append(QString::fromStdString(value));
  }
  return result;
}

std::vector<std::string> strings(const QStringList& values) {
  std::vector<std::string> result;
  for (const auto& value : values) {
    result.push_back(value.toStdString());
  }
  return result;
}

OperationResult outcome(bool success, const std::string& message) {
  OperationResult result;
  result.outcome = success ? OperationOutcome::Accepted : OperationOutcome::Rejected;
  result.message = QString::fromStdString(message);
  return result;
}

template<class Response>
OperationResult standardResult(const Response& response) {
  return outcome(response.success, response.message);
}

}  // namespace

RosOperations::RosOperations() { qRegisterMetaType<OperationResult>(); }

bool RosOperations::available(Operation operation) const {
  const std::lock_guard<std::mutex> lock(queue_mutex_);
  const auto ready = ready_.find(operation);
  return accepting_ && ready != ready_.end() && ready->second;
}

bool RosOperations::submit(const OperationRequest& request) {
  const std::lock_guard<std::mutex> lock(queue_mutex_);
  const auto ready = ready_.find(request.operation);
  if (!accepting_ || ready == ready_.end() || !ready->second) {
    return false;
  }
  queue_.push_back(request);
  return true;
}

template<class Service, class Encode, class Decode>
void RosOperations::add(const rclcpp::Node::SharedPtr& node, Operation operation,
                        const char* service_name, Encode encode, Decode decode) {
  auto client = node->create_client<Service>(service_name);
  clients_.emplace(operation, Client{client,
      [this, client, encode, decode, logger = node->get_logger()](const OperationRequest& input) {
        auto request = std::make_shared<typename Service::Request>();
        encode(input, *request);
        using Future = typename rclcpp::Client<Service>::SharedFuture;
        Pending pending;
        pending.remove = [client](int64_t id) { client->remove_pending_request(id); };
        const auto sequence = std::make_shared<int64_t>(0);
        auto sent = client->async_send_request(request, [this, id = input.id, sequence, decode](Future future) {
          const auto it = pending_.find(id);
          if (it == pending_.end() || !it->second.tracker.markDone(*sequence)) {
            return;
          }
          pending_.erase(it);
          auto result = decode(*future.get());
          result.id = id;
          emit operationCompleted(result);
        });
        *sequence = sent.request_id;
        pending.sequence = *sequence;
        pending.tracker.markSent(*sequence, nowSeconds());
        pending_.emplace(input.id, std::move(pending));
        RCLCPP_INFO(logger,
                    "console_service_request service=%s operation_id=%llu target=%s include_active=%s",
                    client->get_service_name(), static_cast<unsigned long long>(input.id),
                    qPrintable(input.target), input.include_active ? "true" : "false");
      }});
}

void RosOperations::start(const rclcpp::Node::SharedPtr& node) {
  using Remote = fleet::ControlRemoteFleetTask;
  add<Remote>(node, Operation::RemoteTaskControl, "/v2/control_remote_fleet_task",
      [](const auto& input, auto& request) {
        switch (input.remote_action) {
          case RemoteTaskAction::Query: request.operation = Remote::Request::QUERY; break;
          case RemoteTaskAction::Pause: request.operation = Remote::Request::PAUSE; break;
          case RemoteTaskAction::Resume: request.operation = Remote::Request::RESUME; break;
          case RemoteTaskAction::Cancel: request.operation = Remote::Request::CANCEL; break;
          case RemoteTaskAction::Move: request.operation = Remote::Request::MOVE; break;
        }
        request.task_id = input.target.toStdString();
        request.execution_id = input.execution_id;
        request.revision = input.revision;
        request.destination_station = input.to_station.toStdString();
      },
      [](const auto& response) {
        auto result = standardResult(response);
        result.remote_task_id = QString::fromStdString(response.task_id);
        result.remote_state = QString::fromStdString(response.result);
        result.execution_id = response.execution_id;
        result.revision = response.revision;
        result.latest_revision = response.latest_revision;
        if (result.remote_state == QStringLiteral("UNKNOWN")) {
          result.outcome = OperationOutcome::Unknown;
        }
        return result;
      });
  add<std_srvs::srv::SetBool>(node, Operation::SetFleetSimStop, "/v2/set_fleet_sim_stop",
      [](const auto& input, auto& request) { request.data = input.enabled; },
      standardResult<std_srvs::srv::SetBool::Response>);
  add<business::ListScenarioWorkflows>(node, Operation::ListWorkflows, "/v2/list_scenario_workflows",
      [](const auto&, auto&) {},
      [](const auto& response) {
        auto result = standardResult(response);
        const auto count = std::min(response.scenario_ids.size(), response.workflow_ids.size());
        for (size_t i = 0; i < count; ++i) {
          result.workflows.append(WorkflowChoice{
              QString::fromStdString(response.scenario_ids[i]),
              QString::fromStdString(response.workflow_ids[i]),
              i < response.labels.size() ? QString::fromStdString(response.labels[i]) : QString(),
              i < response.default_priorities.size() ? response.default_priorities[i] : 0});
        }
        return result;
      });
  add<business::SubmitOrder>(node, Operation::SubmitWorkflow, "/v2/submit_order",
      [](const auto& input, auto& request) {
        request.order_id = input.target.toStdString();
        request.order_type = "scenario_workflow";
        request.priority = input.priority;
        const QJsonObject payload{
            {QStringLiteral("scenario_id"), input.scenario_id},
            {QStringLiteral("workflow_id"), input.workflow_id},
            {QStringLiteral("start_if_idle"), input.start_if_idle},
            {QStringLiteral("preempt_current"), input.preempt_current}};
        request.payload_json = QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString();
      },
      [](const auto& response) {
        auto result = outcome(response.accepted, response.reject_reason.empty()
                                                     ? response.message : response.reject_reason);
        result.mission_ids = strings(response.mission_ids);
        if (result.mission_ids.isEmpty() && !response.mission_id.empty()) {
          result.mission_ids.append(QString::fromStdString(response.mission_id));
        }
        return result;
      });
  add<business::GetWorkflowStatus>(node, Operation::WorkflowStatus, "/v2/get_workflow_status",
      [](const auto& input, auto& request) { request.order_id = input.target.toStdString(); },
      [](const auto& response) {
        auto result = standardResult(response);
        result.workflow_state = QString::fromStdString(response.state);
        result.paused = response.paused;
        result.running_mission_id = QString::fromStdString(response.running_mission_id);
        result.mission_ids = strings(response.queued_mission_ids);
        result.finished_mission_ids = strings(response.finished_mission_ids);
        return result;
      });
  add<business::PauseWorkflow>(node, Operation::PauseWorkflow, "/v2/pause_workflow",
      [](const auto& input, auto& request) {
        request.order_id = input.target.toStdString();
        request.pause_active = input.include_active;
      },
      [](const auto& response) {
        auto result = standardResult(response);
        result.mission_ids = strings(response.affected_mission_ids);
        result.active_requested = response.active_pause_requested;
        return result;
      });
  add<business::ResumeWorkflow>(node, Operation::ResumeWorkflow, "/v2/resume_workflow",
      [](const auto& input, auto& request) {
        request.order_id = input.target.toStdString();
        request.resume_active = input.include_active;
      },
      [](const auto& response) {
        auto result = standardResult(response);
        result.mission_ids = strings(response.affected_mission_ids);
        result.active_requested = response.active_resume_requested;
        return result;
      });
  add<business::CancelWorkflow>(node, Operation::CancelWorkflow, "/v2/cancel_workflow",
      [](const auto& input, auto& request) {
        request.order_id = input.target.toStdString();
        request.cancel_active = input.include_active;
      },
      [](const auto& response) {
        auto result = standardResult(response);
        result.mission_ids = strings(response.canceled_mission_ids);
        result.active_requested = response.active_cancel_requested;
        return result;
      });
  add<fleet::SubmitFleetStationTask>(node, Operation::SubmitTransport, "/v2/submit_fleet_station_task",
      [](const auto& input, auto& request) {
        request.task_id = input.target.toStdString();
        request.pickup_station = input.from_station.toStdString();
        request.dropoff_station = input.to_station.toStdString();
        request.priority = input.priority;
        request.start_if_idle = input.start_if_idle;
        request.preempt_current = input.preempt_current;
      },
      [](const auto& response) {
        auto result = standardResult(response);
        result.assigned_robot_id = QString::fromStdString(response.assigned_robot_id);
        result.mission_id = QString::fromStdString(response.mission_id);
        return result;
      });
  add<mission::BlockStationRoute>(node, Operation::BlockRoute, "/v2/block_station_route",
      [](const auto& input, auto& request) {
        request.from_station = input.from_station.toStdString();
        request.to_station = input.to_station.toStdString();
        request.reason = input.reason.toStdString();
      }, standardResult<mission::BlockStationRoute::Response>);
  add<mission::UnblockStationRoute>(node, Operation::UnblockRoute, "/v2/unblock_station_route",
      [](const auto& input, auto& request) {
        request.from_station = input.from_station.toStdString();
        request.to_station = input.to_station.toStdString();
      }, standardResult<mission::UnblockStationRoute::Response>);
  add<fleet::UpdateFleetRobotState>(node, Operation::SetRobotEnabled, "/v2/update_fleet_robot_state",
      [](const auto& input, auto& request) {
        request.robot_id = input.target.toStdString();
        request.enabled = input.enabled;
        request.update_enabled = true;
      }, standardResult<fleet::UpdateFleetRobotState::Response>);
  add<mission::PreviewTrafficReservationClear>(node, Operation::PreviewClear,
      "/v2/preview_traffic_reservation_clear",
      [](const auto& input, auto& request) { request.resource_id = input.target.toStdString(); },
      [](const auto& response) {
        auto result = outcome(true, response.message);
        result.preview = {response.found, QString::fromStdString(response.owner_id),
                          strings(response.resource_ids), strings(response.resume_robot_ids)};
        return result;
      });
  add<mission::ClearTrafficReservationIfMatches>(node, Operation::ClearReservation,
      "/v2/clear_traffic_reservation_if_matches",
      [](const auto& input, auto& request) {
        request.resource_id = input.target.toStdString();
        request.expected_owner_id = input.preview.owner_id.toStdString();
        request.expected_resource_ids = strings(input.preview.resource_ids);
        request.expected_resume_robot_ids = strings(input.preview.resume_robot_ids);
      },
      [](const auto& response) {
        using Response = mission::ClearTrafficReservationIfMatches::Response;
        auto result = outcome(response.result == Response::SUCCESS, response.message);
        if (response.result == Response::CONFLICT) {
          result.outcome = OperationOutcome::Conflict;
        }
        return result;
      });
  {
    const std::lock_guard<std::mutex> lock(queue_mutex_);
    accepting_ = true;
  }
  timer_ = node->create_wall_timer(50ms, [this] { poll(); });
}

void RosOperations::poll() {
  std::map<Operation, bool> ready;
  for (const auto& [operation, client] : clients_) {
    ready.emplace(operation, client.ros->service_is_ready());
  }
  std::vector<OperationRequest> requests;
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!accepting_) {
      return;
    }
    changed = ready != ready_;
    ready_ = ready;
    requests.swap(queue_);
  }
  if (changed) {
    emit availabilityChanged();
  }
  for (const auto& request : requests) {
    if (ready.at(request.operation)) {
      try {
        clients_.at(request.operation).send(request);
        continue;
      } catch (const std::exception& error) {
        auto result = outcome(false, error.what());
        result.id = request.id;
        result.outcome = OperationOutcome::Unknown;
        emit operationCompleted(result);
        continue;
      }
    }
    auto result = outcome(false, "service unavailable");
    result.id = request.id;
    emit operationCompleted(result);
  }

  const double now = nowSeconds();
  for (auto it = pending_.begin(); it != pending_.end();) {
    const auto sequence = it->second.tracker.checkTimeout(now);
    if (!sequence) {
      ++it;
      continue;
    }
    it->second.remove(*sequence);
    OperationResult result;
    result.id = it->first;
    result.outcome = OperationOutcome::Unknown;
    result.message = QStringLiteral("服务 3 秒未应答");
    RCLCPP_WARN(rclcpp::get_logger("robot_console"), "console_operation_timeout operation_id=%llu",
                static_cast<unsigned long long>(result.id));
    it = pending_.erase(it);
    emit operationCompleted(result);
  }
}

void RosOperations::stopAccepting() {
  const std::lock_guard<std::mutex> lock(queue_mutex_);
  accepting_ = false;
  ready_.clear();
  queue_.clear();
}

void RosOperations::stop() {
  timer_.reset();
  for (auto& [id, pending] : pending_) {
    pending.remove(pending.sequence);
  }
  pending_.clear();
  clients_.clear();
}

}  // namespace robot_console
