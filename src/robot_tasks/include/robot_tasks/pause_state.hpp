#pragma once

#include <bitset>
#include <cstddef>

namespace robot_tasks {

enum class PauseSource : std::size_t {
  kOperator,
  kWorkflow,
  kLocalization,
  kObstacle,
  kNavigation,
  kCount,
};

class PauseState {
 public:
  bool paused() const { return sources_.any(); }

  bool RunnerPaused() const {
    auto runner_sources = sources_;
    runner_sources.reset(Index(PauseSource::kNavigation));
    return runner_sources.any();
  }

  bool Has(const PauseSource source) const { return sources_.test(Index(source)); }

  void Set(const PauseSource source, const bool active) {
    sources_.set(Index(source), active);
  }

  void Reset() { sources_.reset(); }

  // Navigation feedback alone cannot distinguish independent pauses from
  // acknowledgments of a pause requested by the runner.
  void OnFeedback(const bool feedback_paused) {
    if (!feedback_paused || !paused()) {
      Set(PauseSource::kNavigation, feedback_paused);
    }
  }

 private:
  static constexpr std::size_t Index(const PauseSource source) {
    return static_cast<std::size_t>(source);
  }

  std::bitset<static_cast<std::size_t>(PauseSource::kCount)> sources_;
};

}  // namespace robot_tasks
