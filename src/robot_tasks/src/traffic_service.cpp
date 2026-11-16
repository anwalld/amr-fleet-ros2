#include "robot_tasks/traffic_service.hpp"

#include <algorithm>
#include <chrono>
#include <set>

#include "robot_tasks/sanitize_id.hpp"
#include "robot_tasks/station_catalog.hpp"
#include "robot_tasks/traffic_reservation.hpp"

namespace robot_tasks {

TrafficService::TrafficService(rclcpp::Node* node, MissionContext& context)
    : node_(node), context_(context) {
  v2_block_station_route_srv_ =
      node_->create_service<robot_interfaces_mission::srv::BlockStationRoute>(
          "/v2/block_station_route",
          [this](
              const std::shared_ptr<robot_interfaces_mission::srv::BlockStationRoute::Request>
                  request,
              std::shared_ptr<robot_interfaces_mission::srv::BlockStationRoute::Response>
                  response) { BlockStationRoute(request, response); });
  v2_unblock_station_route_srv_ =
      node_->create_service<robot_interfaces_mission::srv::UnblockStationRoute>(
          "/v2/unblock_station_route",
          [this](
              const std::shared_ptr<robot_interfaces_mission::srv::UnblockStationRoute::Request>
                  request,
              std::shared_ptr<robot_interfaces_mission::srv::UnblockStationRoute::Response>
                  response) { UnblockStationRoute(request, response); });
  v2_list_traffic_reservations_srv_ =
      node_->create_service<robot_interfaces_mission::srv::ListTrafficReservations>(
          "/v2/list_traffic_reservations",
          [this](
              const std::shared_ptr<
                  robot_interfaces_mission::srv::ListTrafficReservations::Request>,
              std::shared_ptr<
                  robot_interfaces_mission::srv::ListTrafficReservations::Response>
                  response) { ListTrafficReservations(response); });
  v2_clear_traffic_reservation_srv_ =
      node_->create_service<robot_interfaces_mission::srv::ClearTrafficReservation>(
          "/v2/clear_traffic_reservation",
          [this](
              const std::shared_ptr<
                  robot_interfaces_mission::srv::ClearTrafficReservation::Request>
                  request,
              std::shared_ptr<
                  robot_interfaces_mission::srv::ClearTrafficReservation::Response>
                  response) { ClearTrafficReservation(request, response); });
  v2_detect_traffic_deadlock_srv_ =
      node_->create_service<robot_interfaces_mission::srv::DetectTrafficDeadlock>(
          "/v2/detect_traffic_deadlock",
          [this](
              const std::shared_ptr<
                  robot_interfaces_mission::srv::DetectTrafficDeadlock::Request>,
              std::shared_ptr<robot_interfaces_mission::srv::DetectTrafficDeadlock::Response>
                  response) { DetectTrafficDeadlock(response); });
  v2_request_route_segment_srv_ =
      node_->create_service<robot_interfaces_mission::srv::RequestRouteSegment>(
          "/v2/request_route_segment",
          [this](
              const std::shared_ptr<robot_interfaces_mission::srv::RequestRouteSegment::Request>
                  request,
              std::shared_ptr<robot_interfaces_mission::srv::RequestRouteSegment::Response>
                  response) { RequestRouteSegmentService(request, response); });
  v2_release_route_segment_srv_ =
      node_->create_service<robot_interfaces_mission::srv::ReleaseRouteSegment>(
          "/v2/release_route_segment",
          [this](
              const std::shared_ptr<robot_interfaces_mission::srv::ReleaseRouteSegment::Request>
                  request,
              std::shared_ptr<robot_interfaces_mission::srv::ReleaseRouteSegment::Response>
                  response) { ReleaseRouteSegmentService(request, response); });
  traffic_state_pub_ =
      node_->create_publisher<robot_interfaces_mission::msg::TrafficState>("/traffic/state", 10);
  traffic_grant_pub_ = node_->create_publisher<robot_interfaces_mission::msg::TrafficGrant>(
      "/traffic/grants", rclcpp::QoS(50).reliable());
  // 1 Hz：兜底补授予（资源可能被任务清理等途径删掉）并发布交通状态。
  traffic_timer_ = node_->create_wall_timer(std::chrono::seconds(1), [this]() {
    GrantWaiters({});
    PublishTrafficState();
  });
}

RouteSegmentDecision TrafficService::RequestRouteSegment(
    const std::string& robot_id, const std::string& from_station, const std::string& to_station,
    const int priority, const std::vector<std::string>& station_path) {
  RouteSegmentDecision decision;
  const auto catalog = robot_tasks::LoadStationCatalog(context_.station_file);
  if (!catalog.has_value()) {
    decision.message = "failed to load station catalog: " + context_.station_file;
    return decision;
  }
  std::vector<std::string> path = station_path;
  if (path.empty()) {
    if (robot_tasks::FindStation(*catalog, to_station) == nullptr) {
      decision.message = "unknown station in route segment request: " + to_station;
      return decision;
    }
    if (!from_station.empty() && from_station != to_station) {
      if (robot_tasks::FindStation(*catalog, from_station) == nullptr) {
        decision.message = "unknown station in route segment request: " + from_station;
        return decision;
      }
      const auto route = robot_tasks::FindRoutePath(*catalog, from_station, to_station);
      if (!route.has_value()) {
        decision.message =
            "no enabled station route: " + from_station + " -> " + to_station;
        return decision;
      }
      path = *route;
    }
  } else {
    for (std::size_t index = 0; index < path.size(); ++index) {
      if (robot_tasks::FindStation(*catalog, path[index]) == nullptr) {
        decision.message = "unknown station in route path: " + path[index];
        return decision;
      }
    }
  }

  RouteSegmentRequest request;
  request.robot_id = robot_id;
  request.priority = priority;
  request.allow_queue = Rolling();
  if (path.size() >= 2U) {
    request.from_station = path.front();
    request.to_station = path.back();
    request.resource_ids = path.size() == 2U
                               ? robot_tasks::BuildRouteSegmentResourceIds(
                                     path[0], path[1], context_.traffic_intersection_locks_enabled)
                               : robot_tasks::BuildRoutePathResourceIds(
                                     path, context_.traffic_intersection_locks_enabled);
  } else {
    // 只占一个路口（车停在 to 上，起点未知或就是 to）。
    request.from_station = from_station;
    request.to_station = to_station;
    request.resource_ids = robot_tasks::BuildRouteSegmentResourceIds(
        "", to_station, context_.traffic_intersection_locks_enabled);
  }
  decision = robot_tasks::PlanRouteSegmentRequest(
      request, context_.route_locks, context_.route_wait_queues,
      &context_.next_route_wait_sequence);
  if (decision.granted) {
    ++grant_count_;
  } else if (decision.newly_queued) {
    ++queue_count_;
  }
  return decision;
}

RouteSegmentReleaseDecision TrafficService::ReleaseRouteSegment(
    const std::string& robot_id, const std::vector<std::string>& resource_ids) {
  auto decision = robot_tasks::PlanRouteSegmentRelease(
      robot_id, resource_ids, context_.route_locks, context_.route_wait_queues);
  for (const auto& grant : decision.granted) {
    NotifyGrant("granted", grant);
  }
  return decision;
}

void TrafficService::WithdrawRouteWaits(const std::string& robot_id) {
  robot_tasks::WithdrawRouteWaits(robot_id, context_.route_wait_queues);
}

void TrafficService::GrantWaiters(const std::vector<std::string>& candidate_resource_ids) {
  if (context_.route_wait_queues.empty()) {
    return;
  }
  const auto granted = robot_tasks::PlanRouteWaitGrants(
      candidate_resource_ids, context_.route_locks, context_.route_wait_queues);
  for (const auto& grant : granted) {
    NotifyGrant("granted", grant);
  }
}

void TrafficService::NotifyGrant(const std::string& event, const RouteGrant& grant) {
  if (event == "granted") {
    ++grant_count_;
  }
  robot_interfaces_mission::msg::TrafficGrant msg;
  msg.stamp = node_->now();
  msg.event = event;
  msg.robot_id = grant.robot_id;
  msg.from_station = grant.from_station;
  msg.to_station = grant.to_station;
  msg.resource_ids = grant.resource_ids;
  traffic_grant_pub_->publish(msg);
  RCLCPP_INFO(
      node_->get_logger(), "traffic %s: %s %s -> %s", event.c_str(), grant.robot_id.c_str(),
      grant.from_station.c_str(), grant.to_station.c_str());
  if (grant_callback_) {
    grant_callback_(event, grant);
  }
}

void TrafficService::PublishTrafficState() {
  robot_interfaces_mission::msg::TrafficState msg;
  msg.stamp = node_->now();
  msg.mode = context_.traffic_reservation_mode;
  std::vector<std::pair<std::string, std::string>> locks(
      context_.route_locks.begin(), context_.route_locks.end());
  std::sort(locks.begin(), locks.end());
  for (const auto& [resource_id, owner_id] : locks) {
    msg.resource_ids.push_back(resource_id);
    msg.owner_ids.push_back(owner_id);
  }
  std::set<std::string> waiting_resources;
  for (const auto& item : context_.route_wait_queues) {
    waiting_resources.insert(item.first);
  }
  for (const auto& resource_id : waiting_resources) {
    const auto& queue = context_.route_wait_queues.at(resource_id);
    for (std::size_t index = 0; index < queue.size(); ++index) {
      msg.waiting_resource_ids.push_back(resource_id);
      msg.waiting_robot_ids.push_back(queue[index].robot_id);
      msg.waiting_priorities.push_back(queue[index].priority);
      msg.waiting_positions.push_back(static_cast<int32_t>(index));
    }
  }
  msg.grant_count = grant_count_;
  msg.queue_count = queue_count_;
  traffic_state_pub_->publish(msg);
}

void TrafficService::RequestRouteSegmentService(
    const std::shared_ptr<robot_interfaces_mission::srv::RequestRouteSegment::Request>& request,
    const std::shared_ptr<robot_interfaces_mission::srv::RequestRouteSegment::Response>&
        response) {
  std::vector<std::string> path;
  for (const auto& station : request->station_path) {
    path.push_back(SanitizeId(station));
  }
  const auto decision = RequestRouteSegment(
      SanitizeId(request->robot_id), SanitizeId(request->from_station),
      SanitizeId(request->to_station), request->priority, path);
  response->success = decision.granted || decision.queued;
  response->granted = decision.granted;
  response->queued = decision.queued;
  response->replan_required = decision.replan_required;
  response->resource_ids = decision.resource_ids;
  response->blocking_resource_ids = decision.blocking_resource_ids;
  response->blocking_owner_ids = decision.blocking_owner_ids;
  response->mode = context_.traffic_reservation_mode;
  response->message = decision.message;
}

void TrafficService::ReleaseRouteSegmentService(
    const std::shared_ptr<robot_interfaces_mission::srv::ReleaseRouteSegment::Request>& request,
    const std::shared_ptr<robot_interfaces_mission::srv::ReleaseRouteSegment::Response>&
        response) {
  const auto robot_id = SanitizeId(request->robot_id);
  if (robot_id.empty()) {
    response->success = false;
    response->message = "release route segment requires a robot_id";
    return;
  }
  const auto decision = ReleaseRouteSegment(robot_id, request->resource_ids);
  response->success = true;
  response->released_resource_ids = decision.released_resource_ids;
  for (const auto& grant : decision.granted) {
    response->granted_robot_ids.push_back(grant.robot_id);
  }
  response->message = decision.message;
}

std::vector<std::string> TrafficService::BuildRouteLockIds(
    const std::vector<std::string>& station_path) const {
  return robot_tasks::BuildRouteLockIds(
      station_path, context_.traffic_intersection_locks_enabled);
}

bool TrafficService::ReserveRouteLocksForMission(
    const std::string& mission_id, const std::vector<std::string>& route_lock_ids,
    std::string* message) {
  return robot_tasks::ReserveRouteLocksForMission(
      context_.route_locks, context_.route_locks_by_mission, mission_id, route_lock_ids,
      message);
}

void TrafficService::ReleaseRouteLocksForMission(const std::string& mission_id) {
  robot_tasks::ReleaseRouteLocksForMission(
      context_.route_locks, context_.route_locks_by_mission, mission_id);
}

void TrafficService::BlockStationRoute(
    const std::shared_ptr<robot_interfaces_mission::srv::BlockStationRoute::Request>& request,
    const std::shared_ptr<robot_interfaces_mission::srv::BlockStationRoute::Response>& response) {
  const auto from = SanitizeId(request->from_station);
  const auto to = SanitizeId(request->to_station);
  const auto lock_id = RouteLockId(from, to);
  const auto catalog = robot_tasks::LoadStationCatalog(context_.station_file);
  if (!catalog.has_value()) {
    response->success = false;
    response->message = "failed to load station catalog: " + context_.station_file;
    response->resource_id = lock_id;
    return;
  }
  const auto reason = SanitizeId(request->reason.empty() ? "manual" : request->reason);
  const auto decision = robot_tasks::PlanTrafficBlock(
      robot_tasks::TrafficBlockRequest{
          from, to, reason,
          robot_tasks::FindStation(*catalog, from) != nullptr &&
              robot_tasks::FindStation(*catalog, to) != nullptr,
          robot_tasks::FindRoutePath(*catalog, from, to).has_value()},
      context_.route_locks);
  if (decision.mutate_state) {
    context_.route_locks[decision.resource_id] = decision.owner_id;
    // 等在被封路段上的车移出队列，通知它们重新规划。
    for (const auto& waiter :
         robot_tasks::PlanRouteBlockWaiterReplan(decision.resource_id, context_.route_wait_queues)) {
      NotifyGrant(
          "replan",
          RouteGrant{waiter.robot_id, waiter.from_station, waiter.to_station, waiter.resource_ids});
    }
  }
  response->success = decision.success;
  response->message = decision.message;
  response->resource_id = decision.resource_id;
}

void TrafficService::UnblockStationRoute(
    const std::shared_ptr<robot_interfaces_mission::srv::UnblockStationRoute::Request>& request,
    const std::shared_ptr<robot_interfaces_mission::srv::UnblockStationRoute::Response>&
        response) {
  const auto from = SanitizeId(request->from_station);
  const auto to = SanitizeId(request->to_station);
  const auto decision = robot_tasks::PlanTrafficUnblock(from, to, context_.route_locks);
  if (decision.mutate_state) {
    context_.route_locks.erase(decision.resource_id);
    GrantWaiters({decision.resource_id});
  }
  response->success = decision.success;
  response->message = decision.message;
  response->resource_id = decision.resource_id;
}

void TrafficService::ListTrafficReservations(
    const std::shared_ptr<robot_interfaces_mission::srv::ListTrafficReservations::Response>&
        response) const {
  response->success = true;
  response->message =
      "loaded " + std::to_string(context_.route_locks.size()) + " traffic reservation(s)";
  for (const auto& [resource_id, owner_id] : context_.route_locks) {
    response->resource_ids.push_back(resource_id);
    response->owner_ids.push_back(owner_id);
    response->reservation_types.push_back(
        robot_tasks::IsTrafficBlockOwner(owner_id) ? "blocked" : "mission");
  }
}

void TrafficService::ClearTrafficReservation(
    const std::shared_ptr<robot_interfaces_mission::srv::ClearTrafficReservation::Request>&
        request,
    const std::shared_ptr<robot_interfaces_mission::srv::ClearTrafficReservation::Response>&
        response) {
  const auto decision =
      robot_tasks::PlanTrafficReservationClear(request->resource_id, context_.route_locks);
  if (decision.erase_resource) {
    context_.route_locks.erase(decision.resource_id);
  }
  if (!decision.release_mission_id.empty()) {
    // 占用者可能是任务号（whole_route）也可能是车辆号（rolling），都整体释放。
    ReleaseRouteLocksForMission(decision.release_mission_id);
  }
  GrantWaiters({});
  response->success = decision.success;
  response->message = decision.message;
  response->owner_id = decision.owner_id;
}

void TrafficService::DetectTrafficDeadlock(
    const std::shared_ptr<robot_interfaces_mission::srv::DetectTrafficDeadlock::Response>&
        response) const {
  response->success = true;
  const auto decision = robot_tasks::PlanTrafficDeadlockDetection(context_.route_locks);
  response->deadlocked = decision.deadlocked;
  response->mission_ids = decision.mission_ids;
  response->conflict_resource_ids = decision.conflict_resource_ids;
  response->conflict_station_ids = decision.conflict_station_ids;
  response->descriptions = decision.descriptions;
  response->message = decision.message;
}

}  // namespace robot_tasks
