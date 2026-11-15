#include "robot_tasks/traffic_service.hpp"

#include "robot_tasks/traffic_reservation.hpp"

namespace robot_tasks {

TrafficService::TrafficService(rclcpp::Node* node, MissionContext& context)
    : node_(node), context_(context) {}

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

}  // namespace robot_tasks
