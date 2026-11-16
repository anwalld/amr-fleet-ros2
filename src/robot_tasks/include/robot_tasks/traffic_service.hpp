#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces_mission/srv/block_station_route.hpp"
#include "robot_interfaces_mission/srv/clear_traffic_reservation.hpp"
#include "robot_interfaces_mission/srv/detect_traffic_deadlock.hpp"
#include "robot_interfaces_mission/srv/list_traffic_reservations.hpp"
#include "robot_interfaces_mission/srv/unblock_station_route.hpp"
#include "robot_tasks/mission_context.hpp"

namespace robot_tasks {

// 路权服务：从 mission_runner_node 拆出的路权表占用/释放，以及封路、解封、
// 占用查询、清除预约、死锁检测五个服务。
// 普通 C++ 类，构造时拿到节点指针和共享上下文，在构造函数里注册服务；
// 状态存在 MissionContext 里，由单线程执行器保证访问安全，不加锁。
// 决策逻辑仍是 traffic_reservation.hpp 的纯函数，这里只做
// “取状态 → 调纯函数 → 写回状态 → 填响应”。
class TrafficService {
 public:
  TrafficService(rclcpp::Node* node, MissionContext& context);

  std::vector<std::string> BuildRouteLockIds(const std::vector<std::string>& station_path) const;
  bool ReserveRouteLocksForMission(
      const std::string& mission_id, const std::vector<std::string>& route_lock_ids,
      std::string* message);
  void ReleaseRouteLocksForMission(const std::string& mission_id);

 private:
  void BlockStationRoute(
      const std::shared_ptr<robot_interfaces_mission::srv::BlockStationRoute::Request>& request,
      const std::shared_ptr<robot_interfaces_mission::srv::BlockStationRoute::Response>& response);
  void UnblockStationRoute(
      const std::shared_ptr<robot_interfaces_mission::srv::UnblockStationRoute::Request>& request,
      const std::shared_ptr<robot_interfaces_mission::srv::UnblockStationRoute::Response>&
          response);
  void ListTrafficReservations(
      const std::shared_ptr<robot_interfaces_mission::srv::ListTrafficReservations::Response>&
          response) const;
  void ClearTrafficReservation(
      const std::shared_ptr<robot_interfaces_mission::srv::ClearTrafficReservation::Request>&
          request,
      const std::shared_ptr<robot_interfaces_mission::srv::ClearTrafficReservation::Response>&
          response);
  void DetectTrafficDeadlock(
      const std::shared_ptr<robot_interfaces_mission::srv::DetectTrafficDeadlock::Response>&
          response) const;

  rclcpp::Node* node_;
  MissionContext& context_;
  rclcpp::Service<robot_interfaces_mission::srv::BlockStationRoute>::SharedPtr
      v2_block_station_route_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::UnblockStationRoute>::SharedPtr
      v2_unblock_station_route_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::ListTrafficReservations>::SharedPtr
      v2_list_traffic_reservations_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::ClearTrafficReservation>::SharedPtr
      v2_clear_traffic_reservation_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::DetectTrafficDeadlock>::SharedPtr
      v2_detect_traffic_deadlock_srv_;
};

}  // namespace robot_tasks
