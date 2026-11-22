#pragma once

namespace robot_tasks {

// 任务中枢的暂停状态，区分暂停是谁发起的（acceptance-suite-rework，specs/mission-control）：
// - 任务中枢发起的（人工、工作流、避障、定位丢失）只能由任务中枢解除。它先置位、再异步通知下层，
//   期间仍会收到"运行中"反馈，按反馈解除会把这些暂停误清掉；
// - 由下层"已暂停"反馈带来的，随下层"运行中"反馈解除。
// 有意不提供到 bool 的隐式转换和赋值：每个改动点都要写明是哪一方。
class PauseState {
 public:
  bool paused() const { return paused_; }

  void SetByRunner(const bool paused) {
    paused_ = paused;
    by_feedback_ = false;
  }

  // 已暂停时由任务中枢接管：避障、定位丢失的规划在"已暂停"时不会再发起暂停，
  // 不接管的话，下层一恢复就会在危险仍在时把暂停解掉。
  void HoldByRunner() { by_feedback_ = false; }

  void OnFeedback(const bool feedback_paused) {
    if (feedback_paused && !paused_) {
      paused_ = true;
      by_feedback_ = true;
    } else if (!feedback_paused && by_feedback_) {
      paused_ = false;
      by_feedback_ = false;
    }
  }

 private:
  bool paused_ = false;
  bool by_feedback_ = false;
};

}  // namespace robot_tasks
