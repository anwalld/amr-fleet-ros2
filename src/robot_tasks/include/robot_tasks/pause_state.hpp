#pragma once

namespace robot_tasks {

// 任务中枢的暂停状态，区分暂停是谁发起的（acceptance-suite-rework，specs/mission-control）：
// - 任务中枢发起的（人工、工作流、避障、定位丢失）不随反馈清除。它先置位、再异步通知下层，
//   期间仍会收到"运行中"反馈，按反馈解除会把这些暂停误清掉；
// - 由下层"已暂停"反馈带来的，随下层"运行中"反馈解除。派发时会先清标记，所以派发前遗留在
//   下层的暂停不论原先由谁发起，都会被记成这一种。
// 这只是任务中枢一侧的标记，不代表车停着：任何人都能直接恢复下层（design D16）。
// 有意不提供到 bool 的隐式转换和赋值：每个改动点都要写明是哪一方。
class PauseState {
 public:
  bool paused() const { return paused_; }

  void SetByRunner(const bool paused) {
    paused_ = paused;
    by_feedback_ = false;
  }

  // 已暂停时出现避障、定位丢失：只把标记改记到任务中枢名下，免得随"运行中"反馈清掉。
  // 不会让下层再停一次，挡不住直接恢复下层。
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
