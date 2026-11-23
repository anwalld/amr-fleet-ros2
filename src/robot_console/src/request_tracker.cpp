#include "robot_console/request_tracker.hpp"

namespace robot_console {

void RequestTracker::markSent(int64_t request_id, double now_s) {
  pending_ = true;
  request_id_ = request_id;
  sent_s_ = now_s;
}

bool RequestTracker::markDone(int64_t request_id) {
  if (!pending_ || request_id != request_id_) return false;
  pending_ = false;
  return true;
}

std::optional<int64_t> RequestTracker::checkTimeout(double now_s) {
  if (!pending_ || now_s - sent_s_ < timeout_s_) return std::nullopt;
  pending_ = false;
  ++timeouts_;
  return request_id_;
}

}  // namespace robot_console
