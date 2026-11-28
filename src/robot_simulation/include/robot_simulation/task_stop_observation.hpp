#pragma once

#include <cmath>
#include <optional>

namespace robot_simulation {

class TaskStopObservation {
 public:
  void Reset() { start_.reset(); }

  bool Observe(const double now, const double x, const double y) {
    if (!start_ || now <= last_sample_ || now - last_sample_ > 0.2 ||
        std::hypot(x - start_x_, y - start_y_) > 0.001) {
      start_ = now;
      start_x_ = x;
      start_y_ = y;
    }
    last_sample_ = now;
    return now - *start_ >= 1.0;
  }

 private:
  std::optional<double> start_;
  double last_sample_ = 0.0;
  double start_x_ = 0.0;
  double start_y_ = 0.0;
};

}  // namespace robot_simulation
