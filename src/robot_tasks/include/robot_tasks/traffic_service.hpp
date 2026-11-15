#pragma once

#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "robot_tasks/mission_context.hpp"

namespace robot_tasks {

// 路权服务：从 mission_runner_node 拆出的路权表占用/释放。
// 普通 C++ 类，构造时拿到节点指针和共享上下文；状态存在 MissionContext 里，
// 由单线程执行器保证访问安全，不加锁。决策逻辑仍是 traffic_reservation.hpp 的纯函数。
class TrafficService {
 public:
  TrafficService(rclcpp::Node* node, MissionContext& context);

  std::vector<std::string> BuildRouteLockIds(const std::vector<std::string>& station_path) const;
  bool ReserveRouteLocksForMission(
      const std::string& mission_id, const std::vector<std::string>& route_lock_ids,
      std::string* message);
  void ReleaseRouteLocksForMission(const std::string& mission_id);

 private:
  rclcpp::Node* node_;
  MissionContext& context_;
};

}  // namespace robot_tasks
