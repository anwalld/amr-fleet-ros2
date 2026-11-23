#pragma once

#include <string>

namespace robot_tasks {

// action 只有收到过取消请求才能以 CANCELED 结束。经 /cancel_mission 取消时没有这个请求，
// 导航序列服务只能 ABORTED + 这条消息，任务中枢据此仍按"已取消"处理、不走失败恢复。
// 结果消息里没有单独的字段，所以两边必须共用这个常量。
inline constexpr char kSequenceCancelledMessage[] = "sequence cancelled";

inline bool IsSequenceCancelled(const bool canceled, const bool aborted,
                                const std::string& message) {
  return canceled || (aborted && message == kSequenceCancelledMessage);
}

}  // namespace robot_tasks
