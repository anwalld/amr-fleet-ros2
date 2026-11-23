#include <gtest/gtest.h>

#include "robot_tasks/nav2_goal_step.hpp"

using robot_tasks::Nav2GoalStatus;
using robot_tasks::Nav2GoalStep;
using robot_tasks::PlanNav2GoalStep;

TEST(Nav2GoalStep, RunningGoalKeepsWaiting) {
  EXPECT_EQ(PlanNav2GoalStep({}), Nav2GoalStep::kWait);
}

TEST(Nav2GoalStep, PauseCancelsRunningGoal) {
  Nav2GoalStatus status;
  status.paused = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kCancelForPause);
}

TEST(Nav2GoalStep, CancelIsRepeatedOnlyWhenDue) {
  Nav2GoalStatus status;
  status.paused = true;
  status.cancelled_for_pause = true;
  status.cancel_due = false;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kHold);
  status.cancel_due = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kCancelForPause);
}

TEST(Nav2GoalStep, FinishedGoalHoldsWhilePaused) {
  Nav2GoalStatus status;
  status.paused = true;
  status.result_ready = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kHold);
  status.succeeded = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kHold);
}

TEST(Nav2GoalStep, ResumeResendsGoalCancelledForPause) {
  Nav2GoalStatus status;
  status.result_ready = true;
  status.cancelled_for_pause = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kResend);
}

TEST(Nav2GoalStep, ResumeWaitsForCancelledGoalToEnd) {
  Nav2GoalStatus status;
  status.cancelled_for_pause = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kWait);
}

TEST(Nav2GoalStep, SuccessThatBeatsCancelIsNotResent) {
  Nav2GoalStatus status;
  status.result_ready = true;
  status.succeeded = true;
  status.cancelled_for_pause = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kSucceed);
}

TEST(Nav2GoalStep, FailureWithoutPauseFails) {
  Nav2GoalStatus status;
  status.result_ready = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kFail);
}

TEST(Nav2GoalStep, StopWinsOverPauseAndResend) {
  Nav2GoalStatus status;
  status.stop_requested = true;
  status.paused = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kStop);
  status.paused = false;
  status.result_ready = true;
  status.cancelled_for_pause = true;
  EXPECT_EQ(PlanNav2GoalStep(status), Nav2GoalStep::kStop);
}
