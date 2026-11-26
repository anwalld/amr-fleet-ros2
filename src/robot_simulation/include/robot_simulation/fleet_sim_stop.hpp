#pragma once

namespace robot_simulation {

inline constexpr double kFleetSimStopStateTtlS = 2.0;

inline bool FleetSimMotionStopped(
    const bool requested_stop, const double last_state_s, const double now_s) {
  return requested_stop || now_s - last_state_s >= kFleetSimStopStateTtlS;
}

}  // namespace robot_simulation
