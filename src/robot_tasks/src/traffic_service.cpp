#include "robot_tasks/traffic_service.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <set>
#include <sstream>

#include "robot_tasks/sanitize_id.hpp"
#include "robot_tasks/station_catalog.hpp"
#include "robot_tasks/traffic_deadlock.hpp"
#include "robot_tasks/traffic_reservation.hpp"

namespace robot_tasks {
namespace {
constexpr auto kRecoveryRequired = "traffic reservations require restart reconciliation";
}

TrafficService::TrafficService(
    rclcpp::Node* node, MissionContext& context, const bool recovery_required)
    : node_(node), context_(context), recovery_required_(recovery_required) {
  if (context_.traffic_service_epoch == 0U) {
    context_.traffic_service_epoch = GenerateTrafficServiceEpoch();
  }
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
  using Preview = robot_interfaces_mission::srv::PreviewTrafficReservationClear;
  preview_traffic_clear_srv_ = node_->create_service<Preview>(
      "/v2/preview_traffic_reservation_clear",
      [this](const Preview::Request::SharedPtr request,
             Preview::Response::SharedPtr response) {
        auto impact = PlanTrafficClearImpact(
            request->resource_id, context_.route_locks, paused_group_of_);
        response->found = impact.decision.success;
        response->owner_id = std::move(impact.decision.owner_id);
        response->message = std::move(impact.decision.message);
        response->resource_ids = std::move(impact.resource_ids);
        response->resume_robot_ids = std::move(impact.resume_robot_ids);
      });
  using ConditionalClear = robot_interfaces_mission::srv::ClearTrafficReservationIfMatches;
  conditional_traffic_clear_srv_ = node_->create_service<ConditionalClear>(
      "/v2/clear_traffic_reservation_if_matches",
      [this](const ConditionalClear::Request::SharedPtr request,
             ConditionalClear::Response::SharedPtr response) {
        if (request->resource_id.empty() || request->expected_owner_id.empty()) {
          response->result = ConditionalClear::Response::ERROR;
          response->message = "resource_id and expected_owner_id are required";
          return;
        }
        const auto impact = PlanTrafficClearImpact(
            request->resource_id, context_.route_locks, paused_group_of_);
        if (!impact.Matches(request->expected_owner_id, request->expected_resource_ids,
                            request->expected_resume_robot_ids)) {
          response->result = ConditionalClear::Response::CONFLICT;
          response->message = "reservation or clear impact changed; preview again";
          return;
        }
        ApplyTrafficClearImpact(
            impact, context_.route_locks, context_.route_locks_by_mission,
            &context_.route_lock_revisions);
        UnfreezePausedRobots(impact.resume_robot_ids);
        GrantWaiters({});
        response->result = ConditionalClear::Response::SUCCESS;
        response->message = impact.decision.message;
      });
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
  traffic_yield_pub_ = node_->create_publisher<robot_interfaces_mission::msg::TrafficYield>(
      "/traffic/yield", rclcpp::QoS(50).reliable());

  // M3 参数。
  wait_starvation_s_ = node_->declare_parameter<double>("traffic_wait_starvation_s", 20.0);
  whole_route_wait_ttl_s_ =
      node_->declare_parameter<double>("traffic_whole_route_wait_ttl_s", 6.0);
  std::istringstream homes(node_->declare_parameter<std::string>("traffic_yield_home_stations", ""));
  std::string home;
  for (std::size_t robot = 1; std::getline(homes, home, ','); ++robot) {
    if (home.empty()) continue;
    const auto [owner, inserted] = yield_home_owners_.emplace(home, "robot_" + std::to_string(robot));
    if (!inserted) owner->second.clear();  // Shared home: no exclusive owner for preference.
  }
  yield_hold_s_ = node_->declare_parameter<double>("traffic_yield_hold_s", 5.0);
  yield_timeout_s_ = node_->declare_parameter<double>("traffic_yield_timeout_s", 10.0);
  deadlock_resolution_enabled_ =
      node_->declare_parameter<bool>("traffic_deadlock_resolution_enabled", true);
  RCLCPP_INFO(
      node_->get_logger(),
      "traffic deadlock: resolution=%s starvation=%.1fs whole_route_wait_ttl=%.1fs "
      "yield_hold=%.1fs yield_timeout=%.1fs",
      deadlock_resolution_enabled_ ? "on" : "off", wait_starvation_s_, whole_route_wait_ttl_s_,
      yield_hold_s_, yield_timeout_s_);

  // 1 Hz：兜底补授予（资源可能被任务清理等途径删掉）、全量死锁检测，并发布交通状态。
  traffic_timer_ = node_->create_wall_timer(std::chrono::seconds(1), [this]() {
    GrantWaiters({});
    RunDeadlockCheck();
    PublishTrafficState();
  });
}

double TrafficService::NowSeconds() const {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

RouteSegmentDecision TrafficService::RequestRouteSegment(
    const std::string& robot_id, const std::string& from_station, const std::string& to_station,
    const int priority, const std::vector<std::string>& station_path,
    const std::string& goal_station) {
  RouteSegmentDecision decision;
  if (recovery_required_) {
    decision.message = kRecoveryRequired;
    return decision;
  }
  if (frozen_robot_ids_.count(robot_id) != 0U) {
    decision.message = "robot " + robot_id +
                       " is paused by deadlock fallback; clear its reservation to resume";
    return decision;
  }
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

  std::string goal = goal_station;
  if (!goal.empty() && robot_tasks::FindStation(*catalog, goal) == nullptr) {
    goal.clear();  // 按为空处理，消息里说明
  }

  RouteSegmentRequest request;
  request.robot_id = robot_id;
  request.priority = priority;
  request.allow_queue = Rolling();
  request.goal_station = goal;
  request.now_s = NowSeconds();
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
      &context_.next_route_wait_sequence, &context_.route_holds,
      &context_.route_lock_revisions, &context_.next_route_lock_revision);
  if (goal != goal_station) {
    decision.message += " (unknown goal_station ignored: " + goal_station + ")";
  }
  bool check_deadlock = false;
  if (decision.granted) {
    ++grant_count_;
    context_.whole_route_retry_waits.erase(robot_id);
  } else if (decision.newly_queued) {
    ++queue_count_;
    check_deadlock = true;
  } else if (!Rolling() && !decision.replan_required && !decision.blocking_resource_ids.empty()) {
    // whole_route 被拒：记为重试等待，在有效期内计入等待图。
    auto& retry = context_.whole_route_retry_waits[robot_id];
    const bool fresh = retry.robot_id.empty() || retry.expires_s <= request.now_s ||
                       retry.blocking_resource_ids != decision.blocking_resource_ids;
    retry = WholeRouteRetryWait{
        robot_id, request.from_station, request.to_station, goal, priority,
        decision.blocking_resource_ids, request.now_s + whole_route_wait_ttl_s_};
    check_deadlock = fresh;
  }
  if (check_deadlock) {
    RunDeadlockCheck();
  }
  return decision;
}

RouteSegmentReleaseDecision TrafficService::ReleaseRouteSegment(
    const std::string& robot_id, const std::vector<std::string>& resource_ids) {
  if (recovery_required_) {
    RouteSegmentReleaseDecision decision;
    decision.message = kRecoveryRequired;
    return decision;
  }
  const RouteWaitGrantOptions options{NowSeconds(), wait_starvation_s_, &frozen_robot_ids_};
  auto decision = robot_tasks::PlanRouteSegmentRelease(
      robot_id, resource_ids, 0U, {}, context_.traffic_service_epoch, context_.route_locks,
      context_.route_wait_queues, &context_.route_lock_revisions,
      &context_.next_route_lock_revision, options, &context_.route_holds);
  for (const auto& grant : decision.granted) {
    NotifyGrant("granted", grant);
  }
  HandlePeelingRelease(robot_id, decision.released_resource_ids);
  return decision;
}

void TrafficService::WithdrawRouteWaits(const std::string& robot_id) {
  robot_tasks::WithdrawRouteWaits(robot_id, context_.route_wait_queues);
  context_.whole_route_retry_waits.erase(robot_id);
}

void TrafficService::GrantWaiters(const std::vector<std::string>& candidate_resource_ids) {
  if (recovery_required_) {
    return;
  }
  if (context_.route_wait_queues.empty()) {
    context_.route_holds.clear();
    return;
  }
  RouteWaitGrantOptions options;
  options.now_s = NowSeconds();
  options.starvation_s = wait_starvation_s_;
  options.frozen_robot_ids = &frozen_robot_ids_;
  const auto granted = robot_tasks::PlanRouteWaitGrants(
      candidate_resource_ids, context_.route_locks, context_.route_wait_queues, options,
      &context_.route_holds, &context_.route_lock_revisions, &context_.next_route_lock_revision,
      context_.traffic_service_epoch);
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
  msg.service_epoch = context_.traffic_service_epoch;
  if (event == "granted") {
    msg.resource_revisions = grant.resource_revisions;
  }
  traffic_grant_pub_->publish(msg);
  RCLCPP_INFO(
      node_->get_logger(), "traffic %s: %s %s -> %s", event.c_str(), grant.robot_id.c_str(),
      grant.from_station.c_str(), grant.to_station.c_str());
  if (grant_callback_) {
    grant_callback_(event, grant);
  }
}

void TrafficService::PublishTrafficState() {
  if (recovery_required_) {
    return;
  }
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
  msg.deadlock_check_count = deadlock_check_count_;
  msg.deadlock_count = deadlock_count_;
  msg.deadlock_robot_ids = deadlock_robot_ids_;
  msg.last_resolution_action = last_resolution_action_;
  msg.last_resolution_robot_id = last_resolution_robot_id_;
  msg.last_resolution_ms = last_resolution_ms_;
  msg.resolved_count = resolved_count_;
  msg.last_detection_us = last_detection_us_;
  msg.max_detection_us = max_detection_us_;
  msg.reroute_count = reroute_count_;
  msg.backoff_count = backoff_count_;
  msg.pause_count = pause_count_;
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
      SanitizeId(request->to_station), request->priority, path,
      SanitizeId(request->goal_station));
  response->success = decision.granted || decision.queued;
  response->granted = decision.granted;
  response->queued = decision.queued;
  response->replan_required = decision.replan_required;
  response->resource_ids = decision.resource_ids;
  response->service_epoch = context_.traffic_service_epoch;
  response->resource_revisions = decision.resource_revisions;
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
  if (recovery_required_) {
    response->success = false;
    response->message = kRecoveryRequired;
    return;
  }
  const RouteWaitGrantOptions options{NowSeconds(), wait_starvation_s_, &frozen_robot_ids_};
  const auto decision = robot_tasks::PlanRouteSegmentRelease(
      robot_id, request->resource_ids, request->expected_service_epoch,
      request->expected_revisions, context_.traffic_service_epoch, context_.route_locks,
      context_.route_wait_queues, &context_.route_lock_revisions,
      &context_.next_route_lock_revision, options, &context_.route_holds);
  if (decision.invalid_expected) {
    response->success = false;
    response->message = decision.message;
    return;
  }
  for (const auto& grant : decision.granted) {
    NotifyGrant("granted", grant);
  }
  HandlePeelingRelease(robot_id, decision.released_resource_ids);
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
    const std::string& mission_id, const std::string& owner_id,
    const std::vector<std::string>& route_lock_ids, std::string* message) {
  if (recovery_required_) {
    if (message != nullptr) {
      *message = kRecoveryRequired;
    }
    return false;
  }
  return robot_tasks::ReserveRouteLocksForMission(
      context_.route_locks, context_.route_locks_by_mission, mission_id, owner_id,
      route_lock_ids, message, &context_.route_lock_revisions, &context_.next_route_lock_revision);
}

void TrafficService::ReleaseRouteLocksForMission(const std::string& mission_id) {
  robot_tasks::ReleaseRouteLocksForMission(
      context_.route_locks, context_.route_locks_by_mission, mission_id,
      &context_.route_lock_revisions);
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
    context_.route_lock_revisions.erase(decision.resource_id);
    GrantWaiters({decision.resource_id});
  }
  response->success = decision.success;
  response->message = decision.message;
  response->resource_id = decision.resource_id;
}

void TrafficService::ListTrafficReservations(
    const std::shared_ptr<robot_interfaces_mission::srv::ListTrafficReservations::Response>&
        response) const {
  if (recovery_required_) {
    response->success = false;
    response->message = kRecoveryRequired;
    return;
  }
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
    context_.route_lock_revisions.erase(decision.resource_id);
  }
  if (!decision.release_mission_id.empty()) {
    // M3 起占用者都是车辆号（旧数据可能残留任务号），释放该占用者持有的全部路权。
    ReleaseRouteLocksForMission(decision.release_mission_id);
    // 兜底暂停的车：运维清除其中任一辆车的占用即恢复整组。
    UnfreezePausedRobots(PausedGroupMembers(paused_group_of_, decision.release_mission_id));
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
  const auto decision = robot_tasks::PlanTrafficDeadlockDetection(
      context_.route_locks, context_.route_wait_queues, context_.whole_route_retry_waits,
      context_.route_holds, NowSeconds());
  response->deadlocked = decision.deadlocked;
  response->mission_ids = decision.mission_ids;
  response->conflict_resource_ids = decision.conflict_resource_ids;
  response->conflict_station_ids = decision.conflict_station_ids;
  response->descriptions = decision.descriptions;
  response->message = decision.message;
}

// ---------------------------------------------------------------------------
// M3 等待图死锁检测与解除
// ---------------------------------------------------------------------------

void TrafficService::HandlePeelingRelease(
    const std::string& robot_id, const std::vector<std::string>& released_resource_ids) {
  if (std::none_of(released_resource_ids.begin(), released_resource_ids.end(),
                   [](const auto& id) { return id.rfind("route_node:", 0) == 0; })) {
    return;
  }
  bool recheck = false;
  for (auto it = active_deadlocks_.begin(); it != active_deadlocks_.end();) {
    if (it->second.peeling_robot_id == robot_id) {
      peeling_recheck_keys_.insert(it->first);
      it = active_deadlocks_.erase(it);
      recheck = true;
    } else {
      ++it;
    }
  }
  if (recheck) {
    RunDeadlockCheck();
  }
}

void TrafficService::RunDeadlockCheck() {
  const double now = NowSeconds();
  robot_tasks::PruneWholeRouteRetryWaits(context_.whole_route_retry_waits, now);

  const auto started = std::chrono::steady_clock::now();
  const auto edges = robot_tasks::BuildWaitGraph(
      context_.route_locks, context_.route_wait_queues, context_.whole_route_retry_waits,
      context_.route_holds, now);
  const auto cycles = robot_tasks::FindWaitCycles(edges);
  last_detection_us_ =
      std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started)
          .count();
  max_detection_us_ = std::max(max_detection_us_, last_detection_us_);
  ++deadlock_check_count_;

  deadlock_robot_ids_.clear();
  for (const auto& cycle : cycles) {
    deadlock_robot_ids_.insert(
        deadlock_robot_ids_.end(), cycle.robot_ids.begin(), cycle.robot_ids.end());
  }
  std::sort(deadlock_robot_ids_.begin(), deadlock_robot_ids_.end());

  const auto update =
      robot_tasks::TrackDeadlocks(active_deadlocks_, cycles, now, yield_timeout_s_);
  for (const auto& resolved : update.resolved) {
    ++resolved_count_;
    last_resolution_action_ = resolved.deadlock.action;
    last_resolution_robot_id_ = resolved.deadlock.yield_robot_id;
    last_resolution_ms_ = resolved.resolution_ms;
    RCLCPP_INFO(
        node_->get_logger(), "traffic deadlock resolved after %.0f ms (%s by %s): %s",
        resolved.resolution_ms,
        resolved.deadlock.action.empty() ? "no action" : resolved.deadlock.action.c_str(),
        resolved.deadlock.yield_robot_id.empty() ? "-" : resolved.deadlock.yield_robot_id.c_str(),
        robot_tasks::DescribeWaitCycle(resolved.deadlock.cycle).c_str());
  }
  std::optional<StationCatalog> catalog;
  if (!update.new_cycles.empty() && deadlock_resolution_enabled_) {
    catalog = robot_tasks::LoadStationCatalog(context_.station_file);
  }
  for (const auto& cycle : update.new_cycles) {
    const auto key = robot_tasks::WaitCycleKey(cycle);
    const bool from_peeling_release = peeling_recheck_keys_.erase(key) > 0U;
    int occurrences = 0;
    if (!from_peeling_release) {
      ++deadlock_count_;
      occurrences = robot_tasks::RecordDeadlockOccurrence(deadlock_history_, key, now);
      RCLCPP_WARN(
          node_->get_logger(), "traffic deadlock detected (#%d in 30s, check %.1f us): %s",
          occurrences, last_detection_us_, robot_tasks::DescribeWaitCycle(cycle).c_str());
    }
    ActiveDeadlock entry{cycle, now, "", ""};

    if (deadlock_resolution_enabled_) {
      if (occurrences >= 3) {
        PauseDeadlockedRobots(cycle, "3rd wait cycle within 30 s");
        entry.action = "pause";
      } else if (!catalog.has_value()) {
        RCLCPP_ERROR(
            node_->get_logger(), "traffic deadlock: failed to load station catalog %s",
            context_.station_file.c_str());
      } else {
        std::unordered_map<std::string, TrafficWaitInfo> info;
        const auto add_wait_info = [&](const std::string& robot_id) {
          if (const auto wait = robot_tasks::FindTrafficWaitInfo(
                  robot_id, context_.route_wait_queues, context_.whole_route_retry_waits, now)) {
            info.emplace(robot_id, *wait);
          }
        };
        for (const auto& robot_id : cycle.robot_ids) {
          add_wait_info(robot_id);
        }
        for (const auto& edge : edges) {
          if (info.count(edge.from_robot_id) == 0U) {
            add_wait_info(edge.from_robot_id);
          }
        }
        const auto plan = robot_tasks::PlanDeadlockResolution(
            cycle, info, *catalog, context_.route_locks, context_.route_holds, edges,
            yield_hold_s_, Rolling(), yield_home_owners_);
        const auto& yielder = plan.robot_id;
        if (plan.action == "pause") {
          PauseDeadlockedRobots(cycle, plan.message);
          entry.action = "pause";
        } else {
          // 撤掉让路车的等待，断开它在等待图上的出边。
          robot_tasks::WithdrawRouteWaits(yielder, context_.route_wait_queues);
          context_.whole_route_retry_waits.erase(yielder);
          PublishYield(plan);
          (plan.action == "reroute" ? reroute_count_ : backoff_count_)++;
          last_resolution_action_ = plan.action;
          last_resolution_robot_id_ = yielder;
          last_resolution_ms_ = -1.0;
          entry.action = plan.action;
          entry.yield_robot_id = yielder;
          if (plan.peeling) {
            entry.peeling_robot_id = yielder;
            entry.peeling_started_s = now;
          }
          RCLCPP_WARN(node_->get_logger(), "traffic deadlock resolution: %s", plan.message.c_str());
        }
      }
    }
    active_deadlocks_[key] = entry;
  }
  peeling_recheck_keys_.clear();
  // 这里不立即补授予：本函数可能在任务中枢自己的申请调用里被触发，立即授予会重入
  // 授予回调。暂扣表由 1 Hz 定时器里的补授予重算。
}

void TrafficService::PauseDeadlockedRobots(const WaitCycle& cycle, const std::string& reason) {
  const auto key = robot_tasks::WaitCycleKey(cycle);
  for (const auto& robot_id : cycle.robot_ids) {
    frozen_robot_ids_.insert(robot_id);
    paused_group_of_[robot_id] = key;
    DeadlockYieldPlan plan;
    plan.robot_id = robot_id;
    plan.action = "pause";
    plan.cycle_robot_ids = cycle.robot_ids;
    PublishYield(plan);
  }
  ++pause_count_;
  last_resolution_action_ = "pause";
  last_resolution_robot_id_ = key;
  last_resolution_ms_ = -1.0;
  RCLCPP_ERROR(
      node_->get_logger(),
      "traffic deadlock fallback: pausing %s (%s); clear one of their reservations via "
      "/v2/clear_traffic_reservation to resume",
      key.c_str(), reason.c_str());
}

void TrafficService::UnfreezePausedRobots(const std::vector<std::string>& robot_ids) {
  if (robot_ids.empty()) {
    return;
  }
  const auto key = paused_group_of_.at(robot_ids.front());
  for (const auto& robot_id : robot_ids) {
    frozen_robot_ids_.erase(robot_id);
    paused_group_of_.erase(robot_id);
  }
  RCLCPP_INFO(node_->get_logger(), "traffic deadlock fallback: resumed %s", key.c_str());
}

void TrafficService::PublishYield(const DeadlockYieldPlan& plan) {
  robot_interfaces_mission::msg::TrafficYield msg;
  msg.stamp = node_->now();
  msg.robot_id = plan.robot_id;
  msg.action = plan.action;
  msg.cycle_robot_ids = plan.cycle_robot_ids;
  msg.avoid_resource_ids = plan.avoid_resource_ids;
  msg.station_path = plan.station_path;
  msg.hold_s = plan.hold_s;
  traffic_yield_pub_->publish(msg);
}

}  // namespace robot_tasks
