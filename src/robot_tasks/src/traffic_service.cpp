#include "robot_tasks/traffic_service.hpp"

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
    ReleaseRouteLocksForMission(decision.release_mission_id);
  }
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
