#include "robot_tasks/pause_state.hpp"

#include <gtest/gtest.h>

namespace robot_tasks {
namespace {

TEST(PauseStateTest, LowerLayerPause_ClearedByRunningFeedback) {
  PauseState state;
  state.OnFeedback(true);
  EXPECT_TRUE(state.paused());
  state.OnFeedback(false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, RunnerPause_SurvivesRunningFeedbackBeforeAndAfterAck) {
  PauseState state;
  state.SetByRunner(true);
  state.OnFeedback(false);  // 下层还没处理暂停
  EXPECT_TRUE(state.paused());
  state.OnFeedback(true);  // 下层确认暂停，不改变发起方
  state.OnFeedback(false);  // 下层被直接恢复，或 Nav2 回调仍在报运行中
  EXPECT_TRUE(state.paused());
}

TEST(PauseStateTest, RunnerPauseOverLowerLayerPause_TakesOwnership) {
  PauseState state;
  state.OnFeedback(true);
  state.SetByRunner(true);
  state.OnFeedback(false);
  EXPECT_TRUE(state.paused());
}

TEST(PauseStateTest, RunnerResume_ClearsEitherKind) {
  PauseState state;
  state.SetByRunner(true);
  state.SetByRunner(false);
  EXPECT_FALSE(state.paused());
  state.OnFeedback(true);
  state.SetByRunner(false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, StalePausedFeedbackAfterRunnerResume_DoesNotStick) {
  PauseState state;
  state.SetByRunner(true);
  state.SetByRunner(false);
  state.OnFeedback(true);  // 下层处理恢复之前发出的最后一条
  state.OnFeedback(false);
  EXPECT_FALSE(state.paused());
}

TEST(PauseStateTest, HazardDuringLowerLayerPause_HeldByRunner) {
  PauseState state;
  state.OnFeedback(true);
  state.HoldByRunner();
  state.OnFeedback(false);
  EXPECT_TRUE(state.paused());
}

TEST(PauseStateTest, HoldWhileNotPaused_DoesNotPause) {
  PauseState state;
  state.HoldByRunner();
  EXPECT_FALSE(state.paused());
}

}  // namespace
}  // namespace robot_tasks

TEST(ReleasePauseAfterCancel, CancelledPausedMissionReleases) {
  EXPECT_TRUE(robot_tasks::ShouldReleasePauseAfterCancel({true, true, false, false}));
}

TEST(ReleasePauseAfterCancel, NothingToReleaseWithoutPauseOrCancel) {
  EXPECT_FALSE(robot_tasks::ShouldReleasePauseAfterCancel({true, false, false, false}));
  EXPECT_FALSE(robot_tasks::ShouldReleasePauseAfterCancel({false, true, false, false}));
}

TEST(ReleasePauseAfterCancel, ObstacleKeepsPause) {
  EXPECT_FALSE(robot_tasks::ShouldReleasePauseAfterCancel({true, true, true, false}));
}

TEST(ReleasePauseAfterCancel, LostLocalizationKeepsPause) {
  EXPECT_FALSE(robot_tasks::ShouldReleasePauseAfterCancel({true, true, false, true}));
}
