#include "robot_tasks/mission_result_workflow.hpp"

#include "robot_tasks/facility_reservation.hpp"

#include <gtest/gtest.h>

namespace robot_tasks {

TEST(MissionResultWorkflowTest, PlanMissionResultWorkflow_Canceled_ClearsCancelAndReleasesResources) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kSucceeded;
  input.result_success = true;
  input.result_message = "operator canceled";
  input.cancellation_requested = true;

  const auto decision = PlanMissionResultWorkflow(input);

  EXPECT_EQ(decision.branch, "canceled");
  EXPECT_TRUE(decision.clear_cancellation_requested);
  EXPECT_TRUE(decision.mark_docking_terminal);
  EXPECT_EQ(decision.docking_terminal_state, "CANCELED");
  EXPECT_TRUE(decision.release_all_resources);
  EXPECT_EQ(decision.state, "CANCELED");
  EXPECT_EQ(decision.state_message, "operator canceled");
  EXPECT_TRUE(decision.recoverable);
  EXPECT_FALSE(decision.handle_failure);
}

TEST(MissionResultWorkflowTest, PlanMissionResultWorkflow_CanceledCode_UsesFallbackMessage) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kCanceled;

  const auto decision = PlanMissionResultWorkflow(input);

  EXPECT_EQ(decision.branch, "canceled");
  EXPECT_EQ(decision.state, "CANCELED");
  EXPECT_EQ(decision.state_message, "mission canceled");
  EXPECT_TRUE(decision.clear_cancellation_requested);
  EXPECT_TRUE(decision.release_all_resources);
  EXPECT_FALSE(decision.handle_failure);
}

TEST(MissionResultWorkflowTest, PlanMissionResultWorkflow_Succeeded_StagesSuccessSideEffects) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kSucceeded;
  input.result_success = true;
  input.result_message = "arrived";

  const auto decision = PlanMissionResultWorkflow(input);

  EXPECT_EQ(decision.branch, "succeeded");
  EXPECT_TRUE(decision.mission_succeeded);
  EXPECT_FALSE(decision.release_all_resources);
  EXPECT_TRUE(decision.release_route_locks);
  EXPECT_TRUE(decision.probe_docking_contact_failure);
  EXPECT_TRUE(decision.reset_failure_retry_count);
  EXPECT_TRUE(decision.mark_resource_occupied);
  EXPECT_TRUE(decision.complete_docking);
  EXPECT_EQ(decision.state, "FINISHED");
  EXPECT_EQ(decision.state_message, "arrived");
  EXPECT_FALSE(decision.handle_failure);
}

TEST(MissionResultWorkflowTest, PlanMissionResultWorkflow_SucceededDock_KeepsDockResourceForCharging) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kSucceeded;
  input.result_success = true;
  input.result_message = "docked";
  input.completed_dock = true;

  const auto decision = PlanMissionResultWorkflow(input);

  EXPECT_EQ(decision.branch, "succeeded");
  EXPECT_TRUE(decision.mission_succeeded);
  EXPECT_FALSE(decision.release_all_resources);
  EXPECT_FALSE(decision.release_route_locks);
  EXPECT_TRUE(decision.mark_resource_occupied);
  EXPECT_TRUE(decision.complete_docking);
  EXPECT_EQ(decision.state, "FINISHED");
}

TEST(MissionResultWorkflowTest, PlanMissionResultWorkflow_SucceededActionWithFailedPayload_FailsMission) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kSucceeded;
  input.result_success = false;
  input.result_message = "goal action succeeded but task payload failed";

  const auto decision = PlanMissionResultWorkflow(input);

  EXPECT_EQ(decision.branch, "failed");
  EXPECT_TRUE(decision.mark_docking_terminal);
  EXPECT_TRUE(decision.release_all_resources);
  EXPECT_TRUE(decision.handle_failure);
  EXPECT_EQ(decision.failure_message, "goal action succeeded but task payload failed");
}

TEST(MissionResultWorkflowTest, PlanMissionResultWorkflow_Failed_StagesRecoverySideEffects) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kFailed;
  input.result_success = false;
  input.result_message = "planner failed";

  const auto decision = PlanMissionResultWorkflow(input);

  EXPECT_EQ(decision.branch, "failed");
  EXPECT_TRUE(decision.mark_docking_terminal);
  EXPECT_EQ(decision.docking_terminal_state, "FAILED");
  EXPECT_EQ(decision.docking_terminal_message, "planner failed");
  EXPECT_TRUE(decision.release_all_resources);
  EXPECT_TRUE(decision.handle_failure);
  EXPECT_EQ(decision.failure_message, "planner failed");
}

TEST(MissionResultWorkflowTest, ShouldQueueDockAfterResult_SuccessfulNonDockEmptyQueue_ReturnsTrue) {
  MissionDockAfterResultRequest request;
  request.mission_succeeded = true;
  request.return_to_dock_after_mission = true;
  request.completed_dock = false;
  request.mission_queue_empty = true;
  request.dock_profile_available = true;

  EXPECT_TRUE(ShouldQueueDockAfterResult(request));
}

TEST(MissionResultWorkflowTest, ShouldQueueDockAfterResult_DockAlreadyCompleted_ReturnsFalse) {
  MissionDockAfterResultRequest request;
  request.mission_succeeded = true;
  request.return_to_dock_after_mission = true;
  request.completed_dock = true;
  request.mission_queue_empty = true;
  request.dock_profile_available = true;

  EXPECT_FALSE(ShouldQueueDockAfterResult(request));
}

TEST(MissionResultWorkflowTest, ShouldQueueDockAfterResult_DockProfileUnavailable_ReturnsFalse) {
  MissionDockAfterResultRequest request;
  request.mission_succeeded = true;
  request.return_to_dock_after_mission = true;
  request.completed_dock = false;
  request.mission_queue_empty = true;
  request.dock_profile_available = false;

  EXPECT_FALSE(ShouldQueueDockAfterResult(request));
}

TEST(MissionResultWorkflowTest, PlanMissionPostResultQueue_AutoStartRunningQueue_RequestsStart) {
  MissionPostResultQueueRequest request;
  request.auto_start_queue = true;
  request.queue_running = true;
  request.mission_queue_empty = false;

  const auto decision = PlanMissionPostResultQueue(request);

  EXPECT_TRUE(decision.queue_start_requested);
  EXPECT_TRUE(decision.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionPostResultQueue_EmptyQueueDropsStaleStartRequest) {
  MissionPostResultQueueRequest request;
  request.existing_queue_start_requested = true;
  request.auto_start_queue = false;
  request.queue_running = false;
  request.mission_queue_empty = true;

  const auto decision = PlanMissionPostResultQueue(request);

  EXPECT_FALSE(decision.queue_start_requested);
  EXPECT_FALSE(decision.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionPostResultQueue_ExistingStartRequestWithQueue_IsPreserved) {
  MissionPostResultQueueRequest request;
  request.existing_queue_start_requested = true;
  request.auto_start_queue = false;
  request.queue_running = false;
  request.mission_queue_empty = false;

  const auto decision = PlanMissionPostResultQueue(request);

  EXPECT_TRUE(decision.queue_start_requested);
  EXPECT_FALSE(decision.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionPostResultQueue_AutoStartDisabled_DoesNotStartQueue) {
  MissionPostResultQueueRequest request;
  request.auto_start_queue = false;
  request.queue_running = true;
  request.mission_queue_empty = false;

  const auto decision = PlanMissionPostResultQueue(request);

  EXPECT_FALSE(decision.queue_start_requested);
  EXPECT_TRUE(decision.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionPostResultQueue_EmptyQueueWithoutStart_StopsQueue) {
  MissionPostResultQueueRequest request;
  request.auto_start_queue = true;
  request.queue_running = true;
  request.mission_queue_empty = true;

  const auto decision = PlanMissionPostResultQueue(request);

  EXPECT_FALSE(decision.queue_start_requested);
  EXPECT_FALSE(decision.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionResultPostActions_ReturnDockStartsQueuedDock) {
  MissionResultPostActionRequest request;
  request.mission_succeeded = true;
  request.return_to_dock_after_mission = true;
  request.completed_dock = false;
  request.mission_queue_empty = true;
  request.dock_profile_available = true;
  request.auto_start_queue = true;

  const auto decision = PlanMissionResultPostActions(request);

  EXPECT_TRUE(decision.queue_dock_after_result);
  EXPECT_TRUE(decision.queue.queue_start_requested);
  EXPECT_TRUE(decision.queue.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionResultPostActions_ExistingQueueKeepsAutoStart) {
  MissionResultPostActionRequest request;
  request.mission_succeeded = true;
  request.return_to_dock_after_mission = false;
  request.mission_queue_empty = false;
  request.auto_start_queue = true;
  request.queue_running = true;

  const auto decision = PlanMissionResultPostActions(request);

  EXPECT_FALSE(decision.queue_dock_after_result);
  EXPECT_TRUE(decision.queue.queue_start_requested);
  EXPECT_TRUE(decision.queue.queue_running);
}

TEST(MissionResultWorkflowTest, PlanMissionResultPostActions_EmptyQueueStopsRunningQueue) {
  MissionResultPostActionRequest request;
  request.mission_succeeded = true;
  request.return_to_dock_after_mission = false;
  request.mission_queue_empty = true;
  request.auto_start_queue = true;
  request.queue_running = true;

  const auto decision = PlanMissionResultPostActions(request);

  EXPECT_FALSE(decision.queue_dock_after_result);
  EXPECT_FALSE(decision.queue.queue_start_requested);
  EXPECT_FALSE(decision.queue.queue_running);
}


namespace {

// 按节点 ApplyMissionResultDecision 的执行顺序作用到设施预约上：先释放全部资源，再标为占用。
// 用来检查决策的最终效果，而不只是标志位（acceptance-suite-rework design D9）。
FacilityReservationMap ApplyFacilityEffects(
    const MissionResultWorkflowDecision& decision, FacilityReservationMap reservations,
    MissionResourceMap& resource_by_mission, const std::string& mission_id) {
  if (decision.release_all_resources) {
    ReleaseResourceForMission(reservations, resource_by_mission, mission_id);
  }
  if (decision.mark_resource_occupied) {
    MarkResourceOccupiedForMission(reservations, resource_by_mission, mission_id);
  }
  return reservations;
}

MissionResultWorkflowDecision SucceededDecision() {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kSucceeded;
  input.result_success = true;
  return PlanMissionResultWorkflow(input);
}

}  // namespace

TEST(MissionResultWorkflowTest, Succeeded_HoldAfterAction_KeepsFacilityOccupied) {
  FacilityReservationMap reservations{
      {"receiving_door", ResourceReservation{"door_alpha", "door_alpha", "reserved", true}}};
  MissionResourceMap by_mission{{"door_alpha", "receiving_door"}};

  const auto after = ApplyFacilityEffects(SucceededDecision(), reservations, by_mission, "door_alpha");

  ASSERT_EQ(after.count("receiving_door"), 1U);
  EXPECT_EQ(after.at("receiving_door").status, "occupied");
  EXPECT_EQ(after.at("receiving_door").holder_id, "door_alpha");
}

TEST(MissionResultWorkflowTest, Succeeded_NoHoldAfterAction_ReleasesFacility) {
  FacilityReservationMap reservations{
      {"freight_elevator", ResourceReservation{"elevator_alpha", "elevator_alpha", "reserved", false}}};
  MissionResourceMap by_mission{{"elevator_alpha", "freight_elevator"}};

  const auto after =
      ApplyFacilityEffects(SucceededDecision(), reservations, by_mission, "elevator_alpha");

  EXPECT_EQ(after.count("freight_elevator"), 0U);
  EXPECT_EQ(by_mission.count("elevator_alpha"), 0U);
}

TEST(MissionResultWorkflowTest, Canceled_ReleasesFacilityEvenWithHold) {
  MissionResultWorkflowInput input;
  input.code = MissionActionResultCode::kCanceled;
  FacilityReservationMap reservations{
      {"receiving_door", ResourceReservation{"door_alpha", "door_alpha", "reserved", true}}};
  MissionResourceMap by_mission{{"door_alpha", "receiving_door"}};

  const auto after =
      ApplyFacilityEffects(PlanMissionResultWorkflow(input), reservations, by_mission, "door_alpha");

  EXPECT_EQ(after.count("receiving_door"), 0U);
}

}  // namespace robot_tasks
