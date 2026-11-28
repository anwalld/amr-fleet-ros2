#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace robot_simulation {

struct PausedTaskProgress {
  bool pickup_leg = true;
  std::string pickup;
  std::string dropoff;
  int priority = 0;
  std::vector<std::string> path;
  std::size_t path_index = 0;
  double dwell_remaining = 0.0;
};

struct ParkedTaskCheckpoint {
  std::string robot_id;
  std::string mode;
  std::string task_id;
  std::string station;
  std::string exit_station;
  std::uint64_t execution = 0;
  std::uint64_t stop_revision = 0;
  std::uint64_t latest_revision = 0;
  double x = 0;
  double y = 0;
  double yaw = 0;
  std::set<std::string> resources;
  bool completed_task = false;
  std::string traffic_authority;
  std::optional<PausedTaskProgress> paused;
};

class ParkedTaskCheckpointStore {
 public:
  enum class Start { kFresh, kExisting, kUnavailable };

  ParkedTaskCheckpointStore() = default;
  ParkedTaskCheckpointStore(const ParkedTaskCheckpointStore&) = delete;
  ParkedTaskCheckpointStore& operator=(const ParkedTaskCheckpointStore&) = delete;
  ~ParkedTaskCheckpointStore() {
    if (lock_fd_ >= 0) {
      close(lock_fd_);
    }
  }

  Start Open(const std::filesystem::path& directory) {
    std::error_code error;
    const bool created = std::filesystem::create_directory(directory, error);
    if (error) {
      return Start::kUnavailable;
    }
    const int fd = open((directory / "owner.lock").c_str(),
                        O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
      return Start::kUnavailable;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
      close(fd);
      return Start::kUnavailable;
    }
    lock_fd_ = fd;
    checkpoint_ = directory / "parked";
    return created ? Start::kFresh : Start::kExisting;
  }

  bool Save(const ParkedTaskCheckpoint& value) const {
    if (lock_fd_ < 0) {
      return false;
    }
    auto draft = checkpoint_;
    draft += ".new";
    std::ofstream out(draft, std::ios::trunc);
    out << "parked-task-v3\n" << std::quoted(value.robot_id) << ' '
        << std::quoted(value.mode) << ' ' << std::quoted(value.task_id) << ' '
        << std::quoted(value.station) << ' ' << std::quoted(value.exit_station) << '\n'
        << value.execution << ' ' << value.stop_revision << ' ' << value.latest_revision
        << ' ' << value.completed_task << '\n'
        << std::setprecision(std::numeric_limits<double>::max_digits10)
        << value.x << ' ' << value.y << ' ' << value.yaw << '\n'
        << std::quoted(value.traffic_authority) << ' ' << value.resources.size() << '\n';
    for (const auto& resource : value.resources) {
      out << std::quoted(resource) << '\n';
    }
    out << value.paused.has_value() << '\n';
    if (value.paused) {
      const auto& progress = *value.paused;
      out << progress.pickup_leg << ' ' << std::quoted(progress.pickup) << ' '
          << std::quoted(progress.dropoff) << ' ' << progress.priority << ' '
          << progress.path_index << ' ' << progress.dwell_remaining << ' '
          << progress.path.size() << '\n';
      for (const auto& station : progress.path) {
        out << std::quoted(station) << '\n';
      }
    }
    out.close();
    if (!out) {
      return false;
    }
    std::error_code error;
    std::filesystem::rename(draft, checkpoint_, error);
    return !error;
  }

  std::optional<ParkedTaskCheckpoint> Read() const {
    if (lock_fd_ < 0) {
      return std::nullopt;
    }
    std::ifstream in(checkpoint_);
    std::string version;
    ParkedTaskCheckpoint value;
    std::size_t resources = 0;
    if (!(in >> version >> std::quoted(value.robot_id) >> std::quoted(value.mode)
          >> std::quoted(value.task_id) >> std::quoted(value.station)
          >> std::quoted(value.exit_station) >> value.execution >> value.stop_revision
          >> value.latest_revision >> value.completed_task >> value.x >> value.y >> value.yaw
          >> std::quoted(value.traffic_authority) >> resources) ||
        (version != "parked-task-v2" && version != "parked-task-v3") || value.task_id.empty() || value.station.empty() ||
        value.execution == 0 || value.stop_revision == 0 || value.traffic_authority.empty() ||
        value.latest_revision < value.stop_revision || resources == 0 || resources > 4096 ||
        !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.yaw)) {
      return std::nullopt;
    }
    for (std::size_t i = 0; i < resources; ++i) {
      std::string resource;
      if (!(in >> std::quoted(resource)) || resource.empty() ||
          !value.resources.insert(resource).second) {
        return std::nullopt;
      }
    }
    if (version == "parked-task-v3") {
      bool paused = false;
      if (!(in >> paused)) {
        return std::nullopt;
      }
      if (paused) {
        PausedTaskProgress progress;
        std::size_t stations = 0;
        if (!(in >> progress.pickup_leg >> std::quoted(progress.pickup)
              >> std::quoted(progress.dropoff) >> progress.priority >> progress.path_index
              >> progress.dwell_remaining >> stations) ||
            progress.pickup.empty() || progress.dropoff.empty() || stations > 4096 ||
            !std::isfinite(progress.dwell_remaining) || progress.dwell_remaining < 0 ||
            (value.exit_station.empty() != (stations == 0)) ||
            (stations > 0 && (progress.path_index == 0 || progress.path_index >= stations))) {
          return std::nullopt;
        }
        for (std::size_t i = 0; i < stations; ++i) {
          std::string station;
          if (!(in >> std::quoted(station)) || station.empty()) {
            return std::nullopt;
          }
          progress.path.push_back(std::move(station));
        }
        value.paused = std::move(progress);
      }
    }
    if ((in >> std::ws).peek() != std::char_traits<char>::eof()) {
      return std::nullopt;
    }
    return value;
  }

  bool Invalidate() const {
    if (lock_fd_ < 0) {
      return false;
    }
    std::error_code error;
    std::filesystem::remove(checkpoint_, error);
    return !error;
  }

 private:
  int lock_fd_ = -1;
  std::filesystem::path checkpoint_;
};

}  // namespace robot_simulation
