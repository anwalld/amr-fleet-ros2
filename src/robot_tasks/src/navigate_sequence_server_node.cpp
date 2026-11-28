#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "geometry_msgs/msg/twist.hpp"
#include "nav2_msgs/action/follow_waypoints.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "robot_interfaces/action/navigate_sequence.hpp"
#include "robot_interfaces/msg/navigation_statistics.hpp"
#include "robot_interfaces/msg/robot_state.hpp"
#include "robot_interfaces/msg/task_status.hpp"
#include "robot_interfaces_core/srv/get_statistics.hpp"
#include "robot_interfaces_mission/srv/set_sequence_pause.hpp"
#include "robot_tasks/nav2_goal_step.hpp"
#include "robot_tasks/sequence_cancel.hpp"
#include "std_srvs/srv/trigger.hpp"

using namespace std::chrono_literals;

using SetSequencePause = robot_interfaces_mission::srv::SetSequencePause;
using NavigateSequence = robot_interfaces::action::NavigateSequence;
using GoalHandleNavigateSequence = rclcpp_action::ServerGoalHandle<NavigateSequence>;
using FollowWaypoints = nav2_msgs::action::FollowWaypoints;
using GoalHandleFollowWaypoints = rclcpp_action::ClientGoalHandle<FollowWaypoints>;
using NavigateToPose = nav2_msgs::action::NavigateToPose;
using GoalHandleNavigateToPose = rclcpp_action::ClientGoalHandle<NavigateToPose>;

class NavigateSequenceServerNode final : public rclcpp::Node {
 public:
  NavigateSequenceServerNode() : Node("navigate_sequence_server_node") {
    const auto checkpoint_path = declare_parameter<std::string>("restart_checkpoint_path", "");
    if (!checkpoint_path.empty()) {
      std::error_code error;
      restart_recovery_required_ = !std::filesystem::create_directory(checkpoint_path, error);
      if (restart_recovery_required_) {
        RCLCPP_ERROR(get_logger(), "navigation inhibited pending restart reconciliation: %s (%s)",
                     checkpoint_path.c_str(), error ? error.message().c_str() : "already used");
      }
    }
    use_nav2_action_ = declare_parameter<bool>("use_nav2_action", true);
    use_waypoint_follower_ = declare_parameter<bool>("use_waypoint_follower", true);
    simulate_without_nav2_ = declare_parameter<bool>("simulate_without_nav2", true);
    waypoint_follower_action_name_ =
        declare_parameter<std::string>("waypoint_follower_action_name", "/follow_waypoints");
    waypoint_follower_server_timeout_ms_ =
        declare_parameter<int>("waypoint_follower_server_timeout_ms", 1000);
    nav2_action_name_ = declare_parameter<std::string>("nav2_action_name", "/navigate_to_pose");
    nav2_server_timeout_ms_ = declare_parameter<int>("nav2_server_timeout_ms", 3000);
    simulated_motion_enabled_ = declare_parameter<bool>("simulated_motion_enabled", false);
    simulated_cmd_vel_topic_ =
        declare_parameter<std::string>("simulated_cmd_vel_topic", "/virtual_rc/cmd_vel");
    simulated_odom_topic_ =
        declare_parameter<std::string>("simulated_odom_topic", "/model/mobile_robot/odometry");
    simulated_goal_tolerance_m_ = declare_parameter<double>("simulated_goal_tolerance_m", 0.22);
    simulated_goal_timeout_s_ = declare_parameter<double>("simulated_goal_timeout_s", 45.0);
    simulated_max_linear_mps_ = declare_parameter<double>("simulated_max_linear_mps", 0.45);
    simulated_max_angular_radps_ = declare_parameter<double>("simulated_max_angular_radps", 1.0);
    current_goal_topic_ =
        declare_parameter<std::string>("current_goal_topic", "/navigate_sequence/current_goal");
    current_path_topic_ =
        declare_parameter<std::string>("current_path_topic", "/navigate_sequence/current_path");

    waypoint_follower_client_ =
        rclcpp_action::create_client<FollowWaypoints>(this, waypoint_follower_action_name_);
    nav2_client_ = rclcpp_action::create_client<NavigateToPose>(this, nav2_action_name_);
    simulated_cmd_pub_ = create_publisher<geometry_msgs::msg::Twist>(simulated_cmd_vel_topic_, 10);
    current_goal_pub_ =
        create_publisher<geometry_msgs::msg::PoseStamped>(current_goal_topic_, rclcpp::QoS(1).transient_local());
    current_path_pub_ =
        create_publisher<nav_msgs::msg::Path>(current_path_topic_, rclcpp::QoS(1).transient_local());
    simulated_odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
        simulated_odom_topic_, 10, [this](nav_msgs::msg::Odometry::SharedPtr msg) {
          std::lock_guard<std::mutex> lock(simulated_pose_mutex_);
          simulated_x_ = msg->pose.pose.position.x;
          simulated_y_ = msg->pose.pose.position.y;
          simulated_yaw_ = YawFromQuaternion(msg->pose.pose.orientation);
          simulated_odom_received_ = true;
        });
    state_pub_ = create_publisher<robot_interfaces::msg::RobotState>("/robot_state", 10);
    task_pub_ = create_publisher<robot_interfaces::msg::TaskStatus>("/task_status", 10);
    v2_stats_srv_ = create_service<robot_interfaces_core::srv::GetStatistics>(
        "/v2/get_statistics",
        [this](const std::shared_ptr<robot_interfaces_core::srv::GetStatistics::Request>,
               std::shared_ptr<robot_interfaces_core::srv::GetStatistics::Response> response) {
          FillStatisticsResponse(response);
        });
    pause_srv_ = create_service<std_srvs::srv::Trigger>(
        "/pause_mission",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          SetPaused(true, response);
        });
    resume_srv_ = create_service<std_srvs::srv::Trigger>(
        "/resume_mission",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          SetPaused(false, response);
        });
    cancel_srv_ = create_service<std_srvs::srv::Trigger>(
        "/cancel_mission",
        [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          cancelled_ = true;
          response->success = true;
          response->message = "mission cancellation requested";
        });
    sequence_pause_srv_ = create_service<SetSequencePause>(
        "/v2/set_sequence_pause",
        [this](const std::shared_ptr<SetSequencePause::Request> request,
               std::shared_ptr<SetSequencePause::Response> response) {
          SetManagedPause(*request, *response);
        });
    action_server_ = rclcpp_action::create_server<NavigateSequence>(
        this, "/navigate_sequence",
        [this](const auto& id, const auto goal) { return HandleGoal(id, goal); },
        [](const auto) {
          // 不在这里置 cancelled_：本回调返回之后目标才进入 CANCELING，各循环只看 is_canceling()。
          return rclcpp_action::CancelResponse::ACCEPT;
        },
        [this](const auto goal_handle) {
          if (worker_.joinable()) {
            worker_.join();
          }
          cancelled_ = false;
          worker_ = std::thread([this, goal_handle]() {
            try {
              Execute(goal_handle);
            } catch (const std::exception&) {
              CompleteGoal([] {});
              // A shutdown can invalidate action handles between a context check and a ROS call.
              if (rclcpp::ok()) {
                throw;
              }
            }
          });
        });
    timer_ = create_wall_timer(200ms, [this]() { PublishState(); });
  }

  ~NavigateSequenceServerNode() override {
    cancelled_ = true;
    InvalidateActiveChildGoalToken();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  class ScopedChildGoalToken {
   public:
    explicit ScopedChildGoalToken(std::atomic<std::uint64_t>& active_token)
        : active_token_(active_token),
          token_(active_token.fetch_add(1U, std::memory_order_acq_rel) + 1U) {}

    ScopedChildGoalToken(const ScopedChildGoalToken&) = delete;
    ScopedChildGoalToken& operator=(const ScopedChildGoalToken&) = delete;

    ~ScopedChildGoalToken() { Invalidate(); }

    std::uint64_t token() const { return token_; }

    void Invalidate() {
      if (!invalidated_) {
        invalidated_ = true;
        active_token_.fetch_add(1U, std::memory_order_acq_rel);
      }
    }

   private:
    std::atomic<std::uint64_t>& active_token_;
    std::uint64_t token_ = 0;
    bool invalidated_ = false;
  };

  void InvalidateActiveChildGoalToken() {
    active_child_goal_token_.fetch_add(1U, std::memory_order_acq_rel);
  }

  template <typename ResponseT>
  void SetPaused(const bool paused, const std::shared_ptr<ResponseT> response) {
    if (!paused && managed_pause_) {
      response->success = false;
      response->message = "mission remains paused by mission runner";
      return;
    }
    legacy_pause_ = paused;
    response->success = true;
    response->message = paused ? "mission paused" : "mission resumed";
  }

  bool IsPaused() const { return legacy_pause_ || managed_pause_; }

  void SetManagedPause(
      const SetSequencePause::Request& request, SetSequencePause::Response& response) {
    const std::lock_guard<std::mutex> lock(goal_control_mutex_);
    response.revision = pause_revision_;
    response.paused = IsPaused();
    response.legacy_paused = legacy_pause_;
    if (!active_goal_id_ || *active_goal_id_ != request.goal_id) {
      response.message = "navigation goal changed";
      return;
    }
    if (request.revision == 0 || request.revision < pause_revision_ ||
        (request.revision == pause_revision_ && request.paused != managed_pause_)) {
      response.message = "stale navigation pause revision";
      return;
    }
    pause_revision_ = request.revision;
    managed_pause_ = request.paused;
    response.revision = pause_revision_;
    response.paused = IsPaused();
    response.success = true;
    response.message = response.paused ? "sequence motion gated" : "sequence gate released";
  }

  template <typename Complete>
  void CompleteGoal(Complete complete) {
    InvalidateActiveChildGoalToken();
    complete();
    const std::lock_guard<std::mutex> lock(goal_control_mutex_);
    active_goal_id_.reset();
    managed_pause_ = false;
    active_ = false;
  }

  template <typename ResponseT>
  void FillStatisticsResponse(const std::shared_ptr<ResponseT>& response) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    response->statistics.header = statistics_.header;
    response->statistics.total_goals = statistics_.total_goals;
    response->statistics.succeeded_goals = statistics_.succeeded_goals;
    response->statistics.failed_goals = statistics_.failed_goals;
    response->statistics.success_rate = statistics_.success_rate;
    response->statistics.total_distance_m = statistics_.total_distance_m;
    response->statistics.last_navigation_time_s = statistics_.last_navigation_time_s;
    response->statistics.mean_navigation_time_s = statistics_.mean_navigation_time_s;
    response->statistics.recovery_count = statistics_.recovery_count;
  }

  rclcpp_action::GoalResponse HandleGoal(
      const rclcpp_action::GoalUUID& id,
      const std::shared_ptr<const NavigateSequence::Goal> goal) {
    const std::lock_guard<std::mutex> lock(goal_control_mutex_);
    if (restart_recovery_required_) {
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (goal->goals.empty()) {
      RCLCPP_WARN(get_logger(), "reject empty navigate sequence goal");
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (active_.exchange(true)) {
      RCLCPP_WARN(get_logger(), "reject navigate sequence while another mission is active");
      return rclcpp_action::GoalResponse::REJECT;
    }
    active_goal_id_ = id;
    pause_revision_ = 0;
    cancelled_ = false;
    RCLCPP_INFO(get_logger(), "accept navigate sequence goal: waypoints=%zu", goal->goals.size());
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  void Execute(const std::shared_ptr<GoalHandleNavigateSequence> goal_handle) {
    const auto goal = goal_handle->get_goal();
    const int total_goals = static_cast<int>(goal->goals.size());
    BeginMission(total_goals);
    PublishCurrentPath(goal->goals);
    const auto started = now();
    int succeeded = 0;
    int failed = 0;

    const bool waypoint_follower_available =
        use_waypoint_follower_ && waypoint_follower_client_->wait_for_action_server(
                                      std::chrono::milliseconds(
                                          waypoint_follower_server_timeout_ms_));
    if (use_waypoint_follower_ && !waypoint_follower_available) {
      RCLCPP_WARN(
          get_logger(), "Nav2 FollowWaypoints action server unavailable, falling back to %s",
          use_nav2_action_ ? "NavigateToPose sequence" : "simulated sequence");
    }
    const bool nav2_available =
        !waypoint_follower_available && use_nav2_action_ &&
        nav2_client_->wait_for_action_server(std::chrono::milliseconds(nav2_server_timeout_ms_));
    if (!waypoint_follower_available && use_nav2_action_ &&
        !nav2_available && !simulate_without_nav2_) {
      FinishFailure(goal_handle, "Nav2 action server is unavailable");
      return;
    }
    if (!waypoint_follower_available && use_nav2_action_ && !nav2_available) {
      RCLCPP_WARN(get_logger(), "Nav2 action server unavailable, using simulated sequence fallback");
    }

    if (waypoint_follower_available) {
      if (!RunWaypointFollowerSequence(goal->goals, goal_handle, succeeded, failed)) {
        FinishCancelled(goal_handle, succeeded, failed);
        return;
      }
    } else {
      for (int i = 0; rclcpp::ok() && i < total_goals; ++i) {
        current_goal_index_.store(i);
        PublishCurrentGoal(goal->goals[i]);
        WaitWhilePaused(goal_handle, "paused before goal");
        if (StopRequested(goal_handle)) {
          FinishCancelled(goal_handle, succeeded, failed);
          return;
        }
        const bool ok = nav2_available ? RunNav2Goal(goal->goals[i], goal_handle)
                                       : RunSimulatedGoal(goal->goals[i], goal_handle);
        if (!ok && StopRequested(goal_handle)) {
          FinishCancelled(goal_handle, succeeded, failed);
          return;
        }
        if (ok) {
          ++succeeded;
          std::lock_guard<std::mutex> lock(stats_mutex_);
          ++statistics_.succeeded_goals;
        } else {
          ++failed;
          std::lock_guard<std::mutex> lock(stats_mutex_);
          ++statistics_.failed_goals;
        }
      }
    }

    const auto finished_stamp = now();
    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      statistics_.last_navigation_time_s = (finished_stamp - started).seconds();
      statistics_.total_goals += total_goals;
      UpdateStatisticsLocked(finished_stamp);
    }
    auto result = std::make_shared<NavigateSequence::Result>();
    result->success = failed == 0;
    result->message = failed == 0 ? "sequence finished" : "sequence finished with failures";
    result->succeeded_goals = succeeded;
    result->failed_goals = failed;
    CompleteGoal([&] { goal_handle->succeed(result); });
  }

  void BeginMission(const int total_goals) {
    current_goal_index_.store(0);
    total_goals_.store(total_goals);
  }

  void PublishCurrentGoal(geometry_msgs::msg::PoseStamped pose) {
    pose.header.stamp = now();
    if (pose.header.frame_id.empty()) {
      pose.header.frame_id = "map";
    }
    current_goal_pub_->publish(pose);
  }

  void PublishCurrentPath(const std::vector<geometry_msgs::msg::PoseStamped>& goals) {
    nav_msgs::msg::Path path;
    path.header.stamp = now();
    path.header.frame_id = goals.empty() || goals.front().header.frame_id.empty()
                                ? "map"
                                : goals.front().header.frame_id;
    path.poses = goals;
    for (auto& pose : path.poses) {
      pose.header.stamp = path.header.stamp;
      if (pose.header.frame_id.empty()) {
        pose.header.frame_id = path.header.frame_id;
      }
    }
    current_path_pub_->publish(path);
  }

  bool RunNav2Goal(const geometry_msgs::msg::PoseStamped& pose,
                   const std::shared_ptr<GoalHandleNavigateSequence>& sequence_handle) {
    while (rclcpp::ok()) {
      switch (RunNav2Attempt(pose, sequence_handle)) {
        case robot_tasks::Nav2GoalStep::kSucceed:
          return true;
        case robot_tasks::Nav2GoalStep::kResend:
          WaitWhilePaused(sequence_handle, "paused before resending Nav2 goal");
          if (StopRequested(sequence_handle)) {
            return false;
          }
          RCLCPP_INFO(get_logger(), "resumed: resending Nav2 goal %d", current_goal_index_.load());
          break;
        default:
          return false;
      }
    }
    return false;
  }

  // 发一次目标并跟到结束，只返回 kSucceed / kResend / kFail / kStop。
  robot_tasks::Nav2GoalStep RunNav2Attempt(
      const geometry_msgs::msg::PoseStamped& pose,
      const std::shared_ptr<GoalHandleNavigateSequence>& sequence_handle) {
    using robot_tasks::Nav2GoalStep;
    NavigateToPose::Goal nav2_goal;
    nav2_goal.pose = pose;

    ScopedChildGoalToken token_guard(active_child_goal_token_);
    const auto goal_token = token_guard.token();
    auto goal_options = rclcpp_action::Client<NavigateToPose>::SendGoalOptions();
    goal_options.feedback_callback =
        [this, sequence_handle, goal_token](
            GoalHandleNavigateToPose::SharedPtr,
            const std::shared_ptr<const NavigateToPose::Feedback> feedback) {
          if (active_child_goal_token_.load(std::memory_order_acquire) != goal_token) {
            return;
          }
          PublishNav2Progress(
              sequence_handle,
              "distance remaining: " + std::to_string(feedback->distance_remaining));
        };
    const auto abandoned = AttachLateCancelGuard(*nav2_client_, goal_options);

    auto goal_future = nav2_client_->async_send_goal(nav2_goal, goal_options);
    if (!AwaitGoalResponse(goal_future, sequence_handle, "waiting for Nav2 goal acceptance")) {
      token_guard.Invalidate();
      AbandonGoalWait(*nav2_client_, goal_future, abandoned);
      return Nav2GoalStep::kStop;
    }
    auto nav2_goal_handle = goal_future.get();
    if (!nav2_goal_handle) {
      token_guard.Invalidate();
      PublishSequenceFeedback(sequence_handle, "failed", "Nav2 rejected goal");
      return Nav2GoalStep::kFail;
    }

    auto result_future = nav2_client_->async_get_result(nav2_goal_handle);
    robot_tasks::Nav2GoalStatus status;
    auto next_cancel = std::chrono::steady_clock::time_point::min();
    while (rclcpp::ok()) {
      status.stop_requested = StopRequested(sequence_handle);
      status.paused = IsPaused();
      status.result_ready = result_future.wait_for(0s) == std::future_status::ready;
      status.succeeded =
          status.result_ready && result_future.get().code == rclcpp_action::ResultCode::SUCCEEDED;
      status.cancel_due = std::chrono::steady_clock::now() >= next_cancel;

      const auto step = robot_tasks::PlanNav2GoalStep(status);
      switch (step) {
        case Nav2GoalStep::kStop:
          token_guard.Invalidate();
          CancelGoal(*nav2_client_, nav2_goal_handle);
          return step;
        case Nav2GoalStep::kSucceed:
        case Nav2GoalStep::kFail:
        case Nav2GoalStep::kResend:
          token_guard.Invalidate();
          return step;
        case Nav2GoalStep::kCancelForPause:
          CancelGoal(*nav2_client_, nav2_goal_handle);
          status.cancelled_for_pause = true;
          next_cancel = std::chrono::steady_clock::now() + 1s;
          [[fallthrough]];
        case Nav2GoalStep::kHold:
          PublishZeroSimulatedCmd();
          PublishNav2Progress(sequence_handle, "Nav2 goal cancelled for pause");
          std::this_thread::sleep_for(100ms);
          break;
        case Nav2GoalStep::kWait:
          // 先等再报：结果一到就回到循环开头处理。
          if (result_future.wait_for(100ms) != std::future_status::ready) {
            PublishNav2Progress(sequence_handle, "Nav2 goal active");
          }
          break;
      }
    }
    token_guard.Invalidate();
    return Nav2GoalStep::kStop;
  }

  template <typename ActionT>
  void CancelGoal(rclcpp_action::Client<ActionT>& client,
                  const typename rclcpp_action::ClientGoalHandle<ActionT>::SharedPtr& handle) {
    try {
      client.async_cancel_goal(handle);
    } catch (const rclcpp_action::exceptions::UnknownGoalHandleError&) {
      // 目标刚好在取消之前结束，客户端已经不认识它，无需再取消。
      RCLCPP_DEBUG(get_logger(), "Nav2 goal finished before it could be cancelled");
    }
  }

  template <typename ActionT>
  std::shared_ptr<std::atomic_bool> AttachLateCancelGuard(
      rclcpp_action::Client<ActionT>& client,
      typename rclcpp_action::Client<ActionT>::SendGoalOptions& goal_options) {
    auto abandoned = std::make_shared<std::atomic_bool>(false);
    goal_options.goal_response_callback =
        [this, &client, abandoned](
            const typename rclcpp_action::ClientGoalHandle<ActionT>::SharedPtr& handle) {
          if (abandoned->load() && handle && rclcpp::ok()) {
            RCLCPP_WARN(get_logger(), "Cancelling late-accepted Nav2 goal after sequence cancel");
            CancelGoal(client, handle);
          }
        };
    return abandoned;
  }

  template <typename ActionT, typename FutureT>
  void AbandonGoalWait(rclcpp_action::Client<ActionT>& client,
                       const FutureT& goal_future,
                       const std::shared_ptr<std::atomic_bool>& abandoned) {
    abandoned->store(true);
    if (goal_future.wait_for(0s) == std::future_status::ready) {
      auto handle = goal_future.get();
      if (handle && rclcpp::ok()) {
        CancelGoal(client, handle);
      }
    }
  }

  // 应答会丢：暂停期间目标没结束就每秒再取消一次；暂停中被取消时退出前也要补一次。
  template <typename ActionT, typename ResultFutureT>
  void CancelWhilePaused(
      rclcpp_action::Client<ActionT>& client,
      const typename rclcpp_action::ClientGoalHandle<ActionT>::SharedPtr& handle,
      const ResultFutureT& result_future,
      const std::shared_ptr<GoalHandleNavigateSequence>& sequence_handle) {
    robot_tasks::Nav2GoalStatus status;
    auto next_cancel = std::chrono::steady_clock::time_point::min();
    while (rclcpp::ok()) {
      status.stop_requested = StopRequested(sequence_handle);
      status.paused = IsPaused();
      status.result_ready = result_future.wait_for(0s) == std::future_status::ready;
      status.cancel_due = std::chrono::steady_clock::now() >= next_cancel;
      const auto step = robot_tasks::PlanNav2GoalStep(status);
      if (step == robot_tasks::Nav2GoalStep::kStop) {
        CancelGoal(client, handle);
        return;
      }
      if (step == robot_tasks::Nav2GoalStep::kCancelForPause) {
        CancelGoal(client, handle);
        next_cancel = std::chrono::steady_clock::now() + 1s;
      } else if (step != robot_tasks::Nav2GoalStep::kHold) {
        return;  // 已恢复
      }
      PublishZeroSimulatedCmd();
      PublishSequenceFeedback(sequence_handle, "paused", "paused during waypoint follower goal");
      std::this_thread::sleep_for(100ms);
    }
  }

  // 暂停期间 Nav2 还会有在途反馈，一律报成暂停，免得任务中枢以为已经恢复。
  void PublishNav2Progress(const std::shared_ptr<GoalHandleNavigateSequence>& sequence_handle,
                           const std::string& message,
                           const std::string& running_state = "navigating") {
    PublishSequenceFeedback(sequence_handle, IsPaused() ? "paused" : running_state, message);
  }

  bool StopRequested(const std::shared_ptr<GoalHandleNavigateSequence>& sequence_handle) const {
    return sequence_handle->is_canceling() || cancelled_;
  }

  bool RunWaypointFollowerSequence(
      const std::vector<geometry_msgs::msg::PoseStamped>& goals,
      const std::shared_ptr<GoalHandleNavigateSequence>& sequence_handle, int& succeeded,
      int& failed) {
    std::size_t start_index = 0;
    while (rclcpp::ok() && start_index < goals.size()) {
      if (sequence_handle->is_canceling() || cancelled_) {
        return false;
      }
      WaitWhilePaused(sequence_handle, "paused before waypoint follower goal");
      if (StopRequested(sequence_handle)) {
        return false;
      }

      FollowWaypoints::Goal follower_goal;
      follower_goal.number_of_loops = 1;
      follower_goal.goal_index = 0;
      follower_goal.poses.assign(goals.begin() + static_cast<long>(start_index), goals.end());
      ScopedChildGoalToken token_guard(active_child_goal_token_);
      const auto goal_token = token_guard.token();
      auto goal_options = rclcpp_action::Client<FollowWaypoints>::SendGoalOptions();
      goal_options.feedback_callback =
          [this, sequence_handle, start_index, goal_token](
              GoalHandleFollowWaypoints::SharedPtr,
              const std::shared_ptr<const FollowWaypoints::Feedback> feedback) {
            if (active_child_goal_token_.load(std::memory_order_acquire) != goal_token) {
              return;
            }
            const auto& seq_goals = sequence_handle->get_goal()->goals;
            const auto absolute_index =
                std::min(start_index + static_cast<std::size_t>(feedback->current_waypoint),
                         seq_goals.empty() ? 0U : seq_goals.size() - 1U);
            current_goal_index_.store(static_cast<int>(absolute_index));
            if (!seq_goals.empty()) {
              PublishCurrentGoal(seq_goals[absolute_index]);
            }
            PublishNav2Progress(
                sequence_handle,
                "Nav2 FollowWaypoints active at waypoint " +
                    std::to_string(static_cast<int>(absolute_index)),
                "following_waypoints");
          };
      const auto abandoned = AttachLateCancelGuard(*waypoint_follower_client_, goal_options);

      auto goal_future = waypoint_follower_client_->async_send_goal(follower_goal, goal_options);
      if (!AwaitGoalResponse(goal_future, sequence_handle, "waiting for Nav2 FollowWaypoints goal")) {
        token_guard.Invalidate();
        AbandonGoalWait(*waypoint_follower_client_, goal_future, abandoned);
        return false;
      }
      auto follower_handle = goal_future.get();
      if (!follower_handle) {
        token_guard.Invalidate();
        const int remaining = static_cast<int>(goals.size() - start_index);
        failed += remaining;
        {
          std::lock_guard<std::mutex> lock(stats_mutex_);
          statistics_.failed_goals += remaining;
        }
        PublishSequenceFeedback(sequence_handle, "failed", "Nav2 FollowWaypoints rejected goal");
        return true;
      }

      auto result_future = waypoint_follower_client_->async_get_result(follower_handle);
      while (rclcpp::ok()) {
        if (sequence_handle->is_canceling() || cancelled_) {
          token_guard.Invalidate();
          CancelGoal(*waypoint_follower_client_, follower_handle);
          return false;
        }
        if (IsPaused()) {
          if (result_future.wait_for(0s) == std::future_status::ready) {
            // 暂停之前已经结束：不取消、不重发，恢复后照常处理这个结果。
            WaitWhilePaused(sequence_handle, "paused after waypoint follower goal");
          } else {
            CancelWhilePaused(
                *waypoint_follower_client_, follower_handle, result_future, sequence_handle);
            if (!StopRequested(sequence_handle) && rclcpp::ok() &&
                result_future.wait_for(0s) == std::future_status::ready &&
                result_future.get().code == rclcpp_action::ResultCode::SUCCEEDED) {
              // 暂停瞬间目标恰已成功：直接落入下方结果结算逻辑，不按 current_goal_index_ 重发。
            } else {
              token_guard.Invalidate();
              start_index = static_cast<std::size_t>(std::max(0, current_goal_index_.load()));
              break;
            }
          }
        }
        if (result_future.wait_for(100ms) == std::future_status::ready) {
          token_guard.Invalidate();
          const auto wrapped = result_future.get();
          const auto remaining = goals.size() - start_index;
          if (wrapped.code != rclcpp_action::ResultCode::SUCCEEDED || !wrapped.result) {
            failed += static_cast<int>(remaining);
            {
              std::lock_guard<std::mutex> lock(stats_mutex_);
              statistics_.failed_goals += static_cast<int>(remaining);
            }
            return true;
          }
          std::unordered_set<std::uint32_t> missed_indexes;
          for (const auto& missed : wrapped.result->missed_waypoints) {
            missed_indexes.insert(missed.index);
          }
          const auto missed_count =
              wrapped.result->error_code == FollowWaypoints::Result::NONE
                  ? static_cast<int>(std::min<std::size_t>(missed_indexes.size(), remaining))
                  : static_cast<int>(remaining);
          failed += missed_count;
          succeeded += static_cast<int>(remaining) - missed_count;
          {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            statistics_.failed_goals += missed_count;
            statistics_.succeeded_goals += static_cast<int>(remaining) - missed_count;
          }
          return true;
        }
        PublishNav2Progress(
            sequence_handle, "Nav2 FollowWaypoints active", "following_waypoints");
      }
    }
    return true;
  }

  bool RunSimulatedGoal(
      const geometry_msgs::msg::PoseStamped& pose,
      const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle) {
    if (simulated_motion_enabled_) {
      return RunSimulatedMotionGoal(pose, goal_handle);
    }
    for (int ticks = 0; rclcpp::ok() && ticks < 4; ++ticks) {
      if (goal_handle->is_canceling() || cancelled_) {
        return false;
      }
      WaitWhilePaused(goal_handle, "paused during simulated goal");
      PublishSequenceFeedback(goal_handle, "navigating", "simulated goal progress");
      std::this_thread::sleep_for(250ms);
    }
    return true;
  }

  bool RunSimulatedMotionGoal(
      const geometry_msgs::msg::PoseStamped& pose,
      const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle) {
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::duration<double>(simulated_goal_timeout_s_);
    while (rclcpp::ok()) {
      const auto pause_check_started = Clock::now();
      if (IsPaused()) {
        WaitWhilePaused(goal_handle, "paused during simulated motion");
        deadline += Clock::now() - pause_check_started;
      }
      if (!rclcpp::ok() || goal_handle->is_canceling() || cancelled_) {
        PublishZeroSimulatedCmd();
        return false;
      }
      if (Clock::now() >= deadline) {
        break;
      }
      const auto command = BuildSimulatedMotionCommand(pose);
      if (command.reached) {
        PublishZeroSimulatedCmd();
        return true;
      }
      simulated_cmd_pub_->publish(command.twist);
      PublishSequenceFeedback(
          goal_handle, "navigating",
          "simulated motion distance remaining: " + std::to_string(command.distance_m));
      std::this_thread::sleep_for(50ms);
    }
    PublishZeroSimulatedCmd();
    PublishSequenceFeedback(goal_handle, "failed", "simulated motion timed out");
    return false;
  }

  struct SimulatedMotionCommand {
    geometry_msgs::msg::Twist twist;
    double distance_m = 0.0;
    bool reached = false;
  };

  SimulatedMotionCommand BuildSimulatedMotionCommand(
      const geometry_msgs::msg::PoseStamped& pose) const {
    double x = 0.0;
    double y = 0.0;
    double yaw = 0.0;
    bool has_odom = false;
    {
      std::lock_guard<std::mutex> lock(simulated_pose_mutex_);
      x = simulated_x_;
      y = simulated_y_;
      yaw = simulated_yaw_;
      has_odom = simulated_odom_received_;
    }
    SimulatedMotionCommand command;
    if (!has_odom) {
      return command;
    }
    const double dx = pose.pose.position.x - x;
    const double dy = pose.pose.position.y - y;
    command.distance_m = std::hypot(dx, dy);
    command.reached = command.distance_m <= simulated_goal_tolerance_m_;
    if (command.reached) {
      return command;
    }
    const double heading_error = NormalizeAngle(std::atan2(dy, dx) - yaw);
    const double linear_scale = std::max(0.0, std::cos(heading_error));
    command.twist.linear.x =
        std::clamp(command.distance_m * 0.8, 0.0, simulated_max_linear_mps_) * linear_scale;
    command.twist.angular.z =
        std::clamp(heading_error * 1.8, -simulated_max_angular_radps_, simulated_max_angular_radps_);
    return command;
  }

  // 等 Nav2 受理目标。暂停时不能卡在这里：目标一被受理车就动了，暂停或取消都要先拿到句柄
  // 才能取消。序列被取消时最多再等 2 秒；返回 false 表示没拿到应答。
  template <typename FutureT>
  bool AwaitGoalResponse(const FutureT& future,
                         const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle,
                         const std::string& message) {
    auto stop_deadline = std::chrono::steady_clock::time_point::max();
    while (future.wait_for(100ms) != std::future_status::ready) {
      const auto tick = std::chrono::steady_clock::now();
      if (!rclcpp::ok() || tick >= stop_deadline) {
        return false;
      }
      if (StopRequested(goal_handle)) {
        stop_deadline = std::min(stop_deadline, tick + 2s);
      } else {
        PublishNav2Progress(goal_handle, message);
      }
    }
    return true;
  }

  void WaitWhilePaused(const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle,
                       const std::string& message) {
    while (rclcpp::ok() && IsPaused() && !cancelled_ && !goal_handle->is_canceling()) {
      PublishZeroSimulatedCmd();
      PublishSequenceFeedback(goal_handle, "paused", message);
      std::this_thread::sleep_for(100ms);
    }
  }

  void PublishZeroSimulatedCmd() const {
    if (simulated_motion_enabled_) {
      geometry_msgs::msg::Twist zero;
      simulated_cmd_pub_->publish(zero);
    }
  }

  static double NormalizeAngle(const double angle) {
    return std::atan2(std::sin(angle), std::cos(angle));
  }

  static double YawFromQuaternion(const geometry_msgs::msg::Quaternion& q) {
    const double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
    const double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    return std::atan2(siny_cosp, cosy_cosp);
  }

  void PublishSequenceFeedback(const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle,
                               const std::string& state, const std::string& message) {
    auto feedback = std::make_shared<NavigateSequence::Feedback>();
    feedback->current_goal_index = current_goal_index_.load();
    feedback->total_goals = total_goals_.load();
    feedback->state = state;
    feedback->message = message;
    if (rclcpp::ok()) {
      try {
        goal_handle->publish_feedback(feedback);
      } catch (const rclcpp::exceptions::RCLError&) {
        if (rclcpp::ok()) {
          throw;
        }
      }
    }
  }

  void FinishFailure(const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle,
                     const std::string& message) {
    const int total_goals = total_goals_.load();
    const auto stamp = now();
    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      statistics_.failed_goals += total_goals;
      statistics_.total_goals += total_goals;
      UpdateStatisticsLocked(stamp);
    }
    auto result = std::make_shared<NavigateSequence::Result>();
    result->success = false;
    result->message = message;
    result->succeeded_goals = 0;
    result->failed_goals = total_goals;
    CompleteGoal([&] { goal_handle->abort(result); });
  }

  void FinishCancelled(const std::shared_ptr<GoalHandleNavigateSequence>& goal_handle,
                       const int succeeded, const int failed) {
    const int total_goals = total_goals_.load();
    const auto stamp = now();
    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      statistics_.failed_goals += 1;
      statistics_.total_goals += total_goals;
      UpdateStatisticsLocked(stamp);
    }
    auto result = std::make_shared<NavigateSequence::Result>();
    result->success = false;
    result->message = robot_tasks::kSequenceCancelledMessage;
    result->succeeded_goals = succeeded;
    result->failed_goals = failed + 1;
    CompleteGoal([&] {
      if (goal_handle->is_canceling()) {
        goal_handle->canceled(result);
      } else {
        // /cancel_mission 没有 action 取消请求，此时 canceled() 会抛异常，见 sequence_cancel.hpp。
        goal_handle->abort(result);
      }
    });
  }

  void UpdateStatisticsLocked(const rclcpp::Time& stamp) {
    const int finished = statistics_.succeeded_goals + statistics_.failed_goals;
    statistics_.header.stamp = stamp;
    statistics_.success_rate =
        finished == 0 ? 0.0 : static_cast<double>(statistics_.succeeded_goals) / finished;
    statistics_.mean_navigation_time_s =
        statistics_.succeeded_goals == 0 ? 0.0 : statistics_.last_navigation_time_s /
                                                static_cast<double>(statistics_.succeeded_goals);
  }

  void PublishState() {
    const int current_index = current_goal_index_.load();
    const int total_goals = total_goals_.load();
    int succeeded_goals = 0;
    int failed_goals = 0;
    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      succeeded_goals = statistics_.succeeded_goals;
      failed_goals = statistics_.failed_goals;
    }

    robot_interfaces::msg::RobotState state;
    state.header.stamp = now();
    state.state = IsPaused() ? "paused" : (active_ ? "navigating" : "idle");
    state.previous_state = previous_state_;
    state.active_goal_id = std::to_string(current_index);
    state.message = active_ ? "mission active" : "waiting for mission";
    state.recoverable = true;
    state_pub_->publish(state);
    previous_state_ = state.state;

    robot_interfaces::msg::TaskStatus task;
    task.header = state.header;
    task.state = state.state;
    task.current_goal_index = current_index;
    task.total_goals = total_goals;
    task.succeeded_goals = succeeded_goals;
    task.failed_goals = failed_goals;
    task.message = state.message;
    task.paused = IsPaused();
    task_pub_->publish(task);
  }

  bool use_nav2_action_;
  bool use_waypoint_follower_;
  bool simulate_without_nav2_;
  bool simulated_motion_enabled_;
  std::string nav2_action_name_;
  std::string waypoint_follower_action_name_;
  std::string simulated_cmd_vel_topic_;
  std::string simulated_odom_topic_;
  std::string current_goal_topic_;
  std::string current_path_topic_;
  int nav2_server_timeout_ms_;
  int waypoint_follower_server_timeout_ms_;
  double simulated_goal_tolerance_m_;
  double simulated_goal_timeout_s_;
  double simulated_max_linear_mps_;
  double simulated_max_angular_radps_;
  std::thread worker_;
  bool restart_recovery_required_ = false;
  std::atomic_bool active_{false};
  std::atomic_bool legacy_pause_{false};
  std::atomic_bool managed_pause_{false};
  std::mutex goal_control_mutex_;
  std::optional<rclcpp_action::GoalUUID> active_goal_id_;
  std::uint64_t pause_revision_ = 0;
  std::atomic_bool cancelled_{false};
  std::atomic<std::uint64_t> active_child_goal_token_{0};
  std::atomic<int> current_goal_index_{0};
  std::atomic<int> total_goals_{0};
  std::string previous_state_ = "initializing";
  mutable std::mutex stats_mutex_;
  robot_interfaces::msg::NavigationStatistics statistics_;
  rclcpp_action::Client<FollowWaypoints>::SharedPtr waypoint_follower_client_;
  rclcpp_action::Client<NavigateToPose>::SharedPtr nav2_client_;
  rclcpp_action::Server<NavigateSequence>::SharedPtr action_server_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr simulated_cmd_pub_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr current_goal_pub_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr current_path_pub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr simulated_odom_sub_;
  rclcpp::Publisher<robot_interfaces::msg::RobotState>::SharedPtr state_pub_;
  rclcpp::Publisher<robot_interfaces::msg::TaskStatus>::SharedPtr task_pub_;
  rclcpp::Service<robot_interfaces_core::srv::GetStatistics>::SharedPtr v2_stats_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr pause_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr resume_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr cancel_srv_;
  rclcpp::Service<SetSequencePause>::SharedPtr sequence_pause_srv_;
  rclcpp::TimerBase::SharedPtr timer_;
  mutable std::mutex simulated_pose_mutex_;
  double simulated_x_ = 0.0;
  double simulated_y_ = 0.0;
  double simulated_yaw_ = 0.0;
  bool simulated_odom_received_ = false;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<NavigateSequenceServerNode>());
  rclcpp::shutdown();
  return 0;
}
