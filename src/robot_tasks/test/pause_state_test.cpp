#include "robot_tasks/pause_state.hpp"

#include <gtest/gtest.h>

namespace robot_tasks {
namespace {

TEST(PauseStateTest, LowerLayerPauseClearedByRunningFeedback) {
  PauseState state;
  state.OnFeedback(true);
  EXPECT_TRUE(state.Has(PauseSource::kNavigation));
  state.OnFeedback(false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, RunnerPauseSurvivesRunningFeedbackBeforeAndAfterAck) {
  PauseState state;
  state.Set(PauseSource::kOperator, true);
  state.OnFeedback(false);
  EXPECT_TRUE(state.paused());
  state.OnFeedback(true);
  EXPECT_FALSE(state.Has(PauseSource::kNavigation));
  state.OnFeedback(false);
  EXPECT_TRUE(state.Has(PauseSource::kOperator));
}

TEST(PauseStateTest, ClearingLocalizationKeepsOperatorPause) {
  PauseState state;
  state.Set(PauseSource::kLocalization, true);
  state.Set(PauseSource::kOperator, true);
  state.Set(PauseSource::kLocalization, false);
  EXPECT_TRUE(state.paused());
  EXPECT_TRUE(state.Has(PauseSource::kOperator));
  state.Set(PauseSource::kOperator, false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, ClearingWorkflowKeepsObstaclePause) {
  PauseState state;
  state.Set(PauseSource::kWorkflow, true);
  state.Set(PauseSource::kObstacle, true);
  state.Set(PauseSource::kWorkflow, false);
  EXPECT_TRUE(state.paused());
  EXPECT_TRUE(state.Has(PauseSource::kObstacle));
}

TEST(PauseStateTest, NavigationResumeDoesNotClearHazard) {
  PauseState state;
  state.OnFeedback(true);
  state.Set(PauseSource::kLocalization, true);
  state.OnFeedback(false);
  EXPECT_FALSE(state.Has(PauseSource::kNavigation));
  EXPECT_TRUE(state.Has(PauseSource::kLocalization));
}

TEST(PauseStateTest, ResetClearsAllTaskSources) {
  PauseState state;
  state.OnFeedback(true);
  state.Set(PauseSource::kOperator, true);
  state.Set(PauseSource::kLocalization, true);
  state.Reset();
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, LowerLayerPause_ClearedByRunningFeedback) {
  PauseState state;
  state.OnFeedback(true);
  EXPECT_TRUE(state.paused());
  state.OnFeedback(false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, RunnerPause_SurvivesRunningFeedbackBeforeAndAfterAck) {
  PauseState state;
  state.Set(PauseSource::kOperator, true);
  state.OnFeedback(false);  // 下层还没处理暂停
  EXPECT_TRUE(state.paused());
  state.OnFeedback(true);  // 下层确认暂停，不改变发起方
  state.OnFeedback(false);  // 下层被直接恢复，或 Nav2 回调仍在报运行中
  EXPECT_TRUE(state.paused());
}

TEST(PauseStateTest, RunnerPauseOverLowerLayerPause_TakesOwnership) {
  PauseState state;
  state.OnFeedback(true);
  state.Set(PauseSource::kOperator, true);
  state.OnFeedback(false);
  EXPECT_TRUE(state.paused());
}

TEST(PauseStateTest, RunnerResume_ClearsEitherKind) {
  PauseState state;
  state.Set(PauseSource::kOperator, true);
  state.Set(PauseSource::kOperator, false);
  state.Set(PauseSource::kNavigation, false);
  EXPECT_FALSE(state.paused());
  state.OnFeedback(true);
  state.Set(PauseSource::kOperator, false);
  state.Set(PauseSource::kNavigation, false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, StalePausedFeedbackAfterRunnerResume_DoesNotStick) {
  PauseState state;
  state.Set(PauseSource::kOperator, true);
  state.Set(PauseSource::kOperator, false);
  state.Set(PauseSource::kNavigation, false);
  state.OnFeedback(true);  // 下层处理恢复之前发出的最后一条
  state.OnFeedback(false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, HazardDuringLowerLayerPause_HeldByRunner) {
  PauseState state;
  state.OnFeedback(true);
  state.Set(PauseSource::kLocalization, state.paused());
  state.OnFeedback(false);
  EXPECT_TRUE(state.paused());
}

TEST(PauseStateTest, HoldWhileNotPaused_DoesNotPause) {
  PauseState state;
  state.Set(PauseSource::kLocalization, state.paused());
  EXPECT_FALSE(state.paused());
}

}  // namespace
}  // namespace robot_tasks
