#pragma once
// 服务请求的超时记账（纯逻辑，不依赖 ROS）：每类请求同时最多挂一个，超过时限记为超时并放弃。
#include <cstdint>
#include <optional>

namespace robot_console {

class RequestTracker {
 public:
  explicit RequestTracker(double timeout_s = 3.0) : timeout_s_(timeout_s) {}

  bool canSend() const { return !pending_; }
  void markSent(int64_t request_id, double now_s);
  // 应答到达。与挂起的请求是同一个时返回 true；超时后才到的迟到应答返回 false。
  bool markDone(int64_t request_id);
  // 挂起超过时限时放弃该请求、超时计数加一，返回被放弃的请求号。
  std::optional<int64_t> checkTimeout(double now_s);

  uint64_t timeouts() const { return timeouts_; }
  bool pending() const { return pending_; }

 private:
  double timeout_s_;
  bool pending_ = false;
  int64_t request_id_ = -1;
  double sent_s_ = 0.0;
  uint64_t timeouts_ = 0;
};

}  // namespace robot_console
