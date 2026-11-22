#include <gtest/gtest.h>

#include "robot_tasks/mission_result_workflow.hpp"
#include "robot_tasks/sequence_cancel.hpp"

using robot_tasks::IsSequenceCancelled;
using robot_tasks::kSequenceCancelledMessage;

TEST(SequenceCancel, CanceledResultIsCancelled) {
  EXPECT_TRUE(IsSequenceCancelled(true, false, ""));
}

TEST(SequenceCancel, AbortFromCancelServiceIsCancelled) {
  EXPECT_TRUE(IsSequenceCancelled(false, true, kSequenceCancelledMessage));
}

TEST(SequenceCancel, OtherAbortStaysFailure) {
  EXPECT_FALSE(IsSequenceCancelled(false, true, "Nav2 action server is unavailable"));
  EXPECT_FALSE(IsSequenceCancelled(false, true, ""));
}

TEST(SequenceCancel, SuccessIsNeverCancelledByMessage) {
  EXPECT_FALSE(IsSequenceCancelled(false, false, kSequenceCancelledMessage));
}

TEST(SequenceCancel, CancelServiceResultDoesNotTriggerFailureRecovery) {
  robot_tasks::MissionResultWorkflowInput input;
  input.result_message = kSequenceCancelledMessage;
  if (IsSequenceCancelled(false, true, input.result_message)) {
    input.code = robot_tasks::MissionActionResultCode::kCanceled;
  }
  const auto decision = robot_tasks::PlanMissionResultWorkflow(input);
  EXPECT_EQ(decision.state, "CANCELED");
  EXPECT_FALSE(decision.handle_failure);
}
