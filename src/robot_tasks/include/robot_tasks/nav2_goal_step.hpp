#pragma once

namespace robot_tasks {

// NavigateToPose 没有暂停：暂停只能取消当前目标，恢复后再发一次同一个目标。
enum class Nav2GoalStep {
  kWait,            // 目标在执行，继续等结果
  kHold,            // 暂停中，不需要再做什么
  kCancelForPause,  // 暂停中且目标还没结束：发取消（应答会丢，所以到期要重发）
  kResend,          // 已恢复，目标是被暂停取消掉的：重发
  kSucceed,
  kFail,
  kStop,  // 序列被取消：取消目标并退出
};

struct Nav2GoalStatus {
  bool stop_requested = false;
  bool paused = false;
  bool result_ready = false;
  bool succeeded = false;
  bool cancelled_for_pause = false;
  bool cancel_due = true;
};

inline Nav2GoalStep PlanNav2GoalStep(const Nav2GoalStatus& status) {
  if (status.stop_requested) {
    return Nav2GoalStep::kStop;
  }
  if (status.paused) {
    return !status.result_ready && status.cancel_due ? Nav2GoalStep::kCancelForPause
                                                     : Nav2GoalStep::kHold;
  }
  if (!status.result_ready) {
    return Nav2GoalStep::kWait;
  }
  if (status.succeeded) {
    return Nav2GoalStep::kSucceed;
  }
  // 被暂停打断后不区分 CANCELED 与 ABORTED：目标真的走不通，重发后会照常失败。
  return status.cancelled_for_pause ? Nav2GoalStep::kResend : Nav2GoalStep::kFail;
}

}  // namespace robot_tasks
