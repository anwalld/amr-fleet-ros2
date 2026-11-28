#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces_mission/srv/block_station_route.hpp"
#include "robot_interfaces_mission/srv/clear_traffic_reservation.hpp"
#include "robot_interfaces_mission/srv/clear_traffic_reservation_if_matches.hpp"
#include "robot_interfaces_mission/srv/preview_traffic_reservation_clear.hpp"
#include "robot_interfaces_mission/srv/detect_traffic_deadlock.hpp"
#include "robot_interfaces_mission/msg/traffic_grant.hpp"
#include "robot_interfaces_mission/msg/traffic_state.hpp"
#include "robot_interfaces_mission/msg/traffic_yield.hpp"
#include "robot_interfaces_mission/srv/list_traffic_reservations.hpp"
#include "robot_interfaces_mission/srv/release_route_segment.hpp"
#include "robot_interfaces_mission/srv/request_route_segment.hpp"
#include "robot_interfaces_mission/srv/unblock_station_route.hpp"
#include "robot_tasks/mission_context.hpp"

namespace robot_tasks {

// 路权服务：从 mission_runner_node 拆出的路权表占用/释放，以及封路、解封、
// 占用查询、清除预约、死锁检测五个服务。
// 普通 C++ 类，构造时拿到节点指针和共享上下文，在构造函数里注册服务；
// 状态存在 MissionContext 里，由单线程执行器保证访问安全，不加锁。
// 决策逻辑仍是 traffic_reservation.hpp 的纯函数，这里只做
// “取状态 → 调纯函数 → 写回状态 → 填响应”。
//
// M2 起还负责滚动申请：/v2/request_route_segment、/v2/release_route_segment 两个服务，
// /traffic/state（1 Hz）与 /traffic/grants（授予 / 重规划通知）两个话题。
// 任务中枢自己的车（robot_1）走同一套 C++ 接口，授予通知通过回调送回节点。
//
// M3 起负责等待图死锁检测与解除：申请新进入等待时、以及每秒一次检测等待环；
// 检出后选让路车，改道 / 退避指令发到 /traffic/yield，反复成环则暂停这组车；
// 统计写进 /traffic/state。纯函数在 traffic_deadlock.hpp。
class TrafficService {
 public:
  // event 为 "granted" 或 "replan"。
  using GrantCallback = std::function<void(const std::string& event, const RouteGrant& grant)>;

  TrafficService(rclcpp::Node* node, MissionContext& context, bool recovery_required = false);

  bool Rolling() const { return context_.traffic_reservation_mode == "rolling"; }
  const std::string& Mode() const { return context_.traffic_reservation_mode; }
  void SetGrantCallback(GrantCallback callback) { grant_callback_ = std::move(callback); }

  // 申请一段路（station_path 非空时按整条路径申请）。rolling 下拿不到排队，
  // whole_route 下拿不到拒绝。会校验站点和路段存在。
  // goal_station：本程目的地（可为空），供死锁解除用。
  RouteSegmentDecision RequestRouteSegment(
      const std::string& robot_id, const std::string& from_station,
      const std::string& to_station, int priority,
      const std::vector<std::string>& station_path = {}, const std::string& goal_station = "");
  // resource_ids 为空 = 释放该车持有的全部资源。新授予会发 /traffic/grants 并回调。
  RouteSegmentReleaseDecision ReleaseRouteSegment(
      const std::string& robot_id, const std::vector<std::string>& resource_ids);
  // 撤掉该车的等待（取消任务时）。
  void WithdrawRouteWaits(const std::string& robot_id);

  std::vector<std::string> BuildRouteLockIds(const std::vector<std::string>& station_path) const;
  bool ReserveRouteLocksForMission(
      const std::string& mission_id, const std::string& owner_id,
      const std::vector<std::string>& route_lock_ids, std::string* message);
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

  void RequestRouteSegmentService(
      const std::shared_ptr<robot_interfaces_mission::srv::RequestRouteSegment::Request>& request,
      const std::shared_ptr<robot_interfaces_mission::srv::RequestRouteSegment::Response>&
          response);
  void ReleaseRouteSegmentService(
      const std::shared_ptr<robot_interfaces_mission::srv::ReleaseRouteSegment::Request>& request,
      const std::shared_ptr<robot_interfaces_mission::srv::ReleaseRouteSegment::Response>&
          response);
  // 资源被非常规途径删掉后（解封、清除预约、任务清理），给等待者补授予。
  void GrantWaiters(const std::vector<std::string>& candidate_resource_ids);
  void NotifyGrant(const std::string& event, const RouteGrant& grant);
  void PublishTrafficState();
  // M3：检测一次等待环，按需解除并更新统计。
  void RunDeadlockCheck();
  void HandlePeelingRelease(
      const std::string& robot_id, const std::vector<std::string>& released_resource_ids);
  void PauseDeadlockedRobots(const WaitCycle& cycle, const std::string& reason);
  void PublishYield(const DeadlockYieldPlan& plan);
  // 运维清除了某车的占用：解除它所在暂停组。
  void UnfreezePausedRobots(const std::vector<std::string>& robot_ids);
  double NowSeconds() const;

  rclcpp::Node* node_;
  MissionContext& context_;
  const bool recovery_required_;
  GrantCallback grant_callback_;
  std::uint64_t grant_count_ = 0;
  std::uint64_t queue_count_ = 0;
  // M3 参数。
  double wait_starvation_s_ = 20.0;
  double whole_route_wait_ttl_s_ = 6.0;
  std::unordered_map<std::string, std::string> yield_home_owners_;
  double yield_hold_s_ = 5.0;
  double yield_timeout_s_ = 10.0;
  bool deadlock_resolution_enabled_ = true;
  // M3 状态。
  DeadlockHistory deadlock_history_;
  ActiveDeadlockMap active_deadlocks_;
  std::unordered_set<std::string> peeling_recheck_keys_;
  std::unordered_set<std::string> frozen_robot_ids_;
  std::unordered_map<std::string, std::string> paused_group_of_;
  std::vector<std::string> deadlock_robot_ids_;
  // M3 统计（进 /traffic/state）。
  std::uint64_t deadlock_check_count_ = 0;
  std::uint64_t deadlock_count_ = 0;
  std::uint64_t resolved_count_ = 0;
  std::uint64_t reroute_count_ = 0;
  std::uint64_t backoff_count_ = 0;
  std::uint64_t pause_count_ = 0;
  std::string last_resolution_action_;
  std::string last_resolution_robot_id_;
  double last_resolution_ms_ = -1.0;
  double last_detection_us_ = 0.0;
  double max_detection_us_ = 0.0;
  rclcpp::Publisher<robot_interfaces_mission::msg::TrafficYield>::SharedPtr traffic_yield_pub_;
  rclcpp::Service<robot_interfaces_mission::srv::RequestRouteSegment>::SharedPtr
      v2_request_route_segment_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::ReleaseRouteSegment>::SharedPtr
      v2_release_route_segment_srv_;
  rclcpp::Publisher<robot_interfaces_mission::msg::TrafficState>::SharedPtr traffic_state_pub_;
  rclcpp::Publisher<robot_interfaces_mission::msg::TrafficGrant>::SharedPtr traffic_grant_pub_;
  rclcpp::TimerBase::SharedPtr traffic_timer_;
  rclcpp::Service<robot_interfaces_mission::srv::BlockStationRoute>::SharedPtr
      v2_block_station_route_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::UnblockStationRoute>::SharedPtr
      v2_unblock_station_route_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::ListTrafficReservations>::SharedPtr
      v2_list_traffic_reservations_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::ClearTrafficReservation>::SharedPtr
      v2_clear_traffic_reservation_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::PreviewTrafficReservationClear>::SharedPtr
      preview_traffic_clear_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::ClearTrafficReservationIfMatches>::SharedPtr
      conditional_traffic_clear_srv_;
  rclcpp::Service<robot_interfaces_mission::srv::DetectTrafficDeadlock>::SharedPtr
      v2_detect_traffic_deadlock_srv_;
};

}  // namespace robot_tasks
