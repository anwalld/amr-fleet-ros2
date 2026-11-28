#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "robot_tasks/fleet_runtime_state.hpp"
#include "robot_tasks/mission_queue.hpp"

namespace robot_tasks {

struct RestartReceipt {
  bool success = false;
  std::string message;
  std::uint64_t latest_revision = 0;
  std::string result;
};

struct MissionRestartSnapshot {
  std::string robot_id;
  RemoteFleetTaskMap remote_tasks;
  std::map<std::tuple<std::string, std::uint64_t, std::uint64_t>, RestartReceipt> receipts;
  std::vector<QueuedMission> queue;
  std::unordered_set<std::string> paused_workflows;
  std::string mission_id;
  std::string goal_uuid;
  std::uint64_t pause_revision = 0;
  unsigned pause_mask = 0;
  bool mission_busy = false;
  bool dispatch_held = false;
};

class MissionRestartStore {
 public:
  enum class Start { kFresh, kExisting, kUnavailable };
  MissionRestartStore() = default;
  MissionRestartStore(const MissionRestartStore&) = delete;
  MissionRestartStore& operator=(const MissionRestartStore&) = delete;
  ~MissionRestartStore() {
    if (owner_ >= 0) {
      close(owner_);
    }
  }

  Start Open(const std::filesystem::path& directory) {
    std::error_code error;
    const bool created = std::filesystem::create_directory(directory, error);
    if (error) {
      return Start::kUnavailable;
    }
    const int fd = open((directory / "mission-owner.lock").c_str(),
                        O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
      return Start::kUnavailable;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
      close(fd);
      return Start::kUnavailable;
    }
    owner_ = fd;
    file_ = directory / "mission-v1";
    return created ? Start::kFresh : Start::kExisting;
  }

  bool Save(const MissionRestartSnapshot& snapshot) {
    if (owner_ < 0) {
      return false;
    }
    std::ostringstream stream;
    Write(stream, snapshot);
    auto encoded = std::move(stream).str();
    if (encoded == last_saved_) {
      return true;
    }
    auto draft = file_;
    draft += ".new";
    std::ofstream out(draft, std::ios::trunc);
    out << encoded;
    out.close();
    if (!out) {
      return false;
    }
    std::error_code error;
    std::filesystem::rename(draft, file_, error);
    if (error) {
      return false;
    }
    last_saved_ = std::move(encoded);
    return true;
  }

  std::optional<MissionRestartSnapshot> Read() const {
    if (owner_ < 0) {
      return std::nullopt;
    }
    std::ifstream in(file_);
    MissionRestartSnapshot data;
    std::string version;
    if (!(in >> version >> std::quoted(data.robot_id) >> std::quoted(data.mission_id)
          >> std::quoted(data.goal_uuid) >> data.pause_revision >> data.pause_mask
          >> data.mission_busy >> data.dispatch_held) ||
        version != "mission-v1" || data.robot_id.empty() || data.pause_mask > 31 ||
        (!data.goal_uuid.empty() && data.goal_uuid.find_first_not_of("0123456789abcdef") !=
                                     std::string::npos) ||
        (!data.goal_uuid.empty() && data.goal_uuid.size() != 32)) {
      return std::nullopt;
    }
    if (!ReadTasks(in, data) || !ReadQueue(in, data) ||
        (in >> std::ws).peek() != std::char_traits<char>::eof()) {
      return std::nullopt;
    }
    return data;
  }

 private:
  static constexpr std::size_t kMaxEntries = 100000;

  static void Write(std::ostream& out, const MissionRestartSnapshot& data) {
    out << "mission-v1\n" << std::quoted(data.robot_id) << ' ' << std::quoted(data.mission_id)
        << ' ' << std::quoted(data.goal_uuid) << ' ' << data.pause_revision << ' '
        << data.pause_mask << ' ' << data.mission_busy << ' ' << data.dispatch_held << '\n';
    out << data.remote_tasks.size() << '\n';
    for (const auto& [id, task] : data.remote_tasks) {
      out << std::quoted(id) << ' ' << std::quoted(task.assigned_robot_id) << ' '
          << std::quoted(task.pickup_station) << ' ' << std::quoted(task.dropoff_station) << ' '
          << std::quoted(task.state) << ' ' << std::quoted(task.message) << ' ' << task.priority << '\n';
    }
    out << data.receipts.size() << '\n';
    for (const auto& [key, receipt] : data.receipts) {
      const auto& [task_id, execution, revision] = key;
      out << std::quoted(task_id) << ' ' << execution << ' ' << revision << ' '
          << receipt.success << ' ' << std::quoted(receipt.message) << ' '
          << receipt.latest_revision << ' ' << std::quoted(receipt.result) << '\n';
    }
    out << data.queue.size() << '\n' << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& entry : data.queue) {
      const auto& profile = entry.profile;
      out << std::quoted(profile.mission_id) << ' ' << std::quoted(profile.frame_id) << ' '
          << profile.loop << ' ' << std::quoted(entry.mission_file) << ' ' << entry.priority << ' '
          << entry.sequence << ' ' << profile.waypoints.size() << '\n';
      for (const auto& point : profile.waypoints) {
        out << point.x << ' ' << point.y << ' ' << point.yaw << '\n';
      }
    }
    out << data.paused_workflows.size() << '\n';
    for (const auto& order : data.paused_workflows) {
      out << std::quoted(order) << '\n';
    }
  }

  static bool ReadCount(std::istream& in, std::size_t& count) {
    return (in >> count) && count <= kMaxEntries;
  }

  static bool ReadTasks(std::istream& in, MissionRestartSnapshot& data) {
    std::size_t count = 0;
    if (!ReadCount(in, count)) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      RemoteFleetTaskState task;
      if (!(in >> std::quoted(task.task_id) >> std::quoted(task.assigned_robot_id)
            >> std::quoted(task.pickup_station) >> std::quoted(task.dropoff_station)
            >> std::quoted(task.state) >> std::quoted(task.message) >> task.priority) ||
          task.task_id.empty() || task.assigned_robot_id.empty() || task.state.empty()) {
        return false;
      }
      const auto id = task.task_id;
      if (!data.remote_tasks.emplace(id, std::move(task)).second) {
        return false;
      }
    }
    if (!ReadCount(in, count)) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      std::string task;
      std::uint64_t execution = 0, revision = 0;
      RestartReceipt receipt;
      if (!(in >> std::quoted(task) >> execution >> revision >> receipt.success
            >> std::quoted(receipt.message) >> receipt.latest_revision >> std::quoted(receipt.result)) ||
          execution == 0 || revision == 0 || data.remote_tasks.count(task) == 0 ||
          receipt.result.empty() ||
          !data.receipts.emplace(std::make_tuple(task, execution, revision), std::move(receipt)).second) {
        return false;
      }
    }
    return true;
  }

  static bool ReadQueue(std::istream& in, MissionRestartSnapshot& data) {
    std::size_t count = 0;
    if (!ReadCount(in, count)) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      QueuedMission entry;
      auto& profile = entry.profile;
      std::size_t waypoints = 0;
      if (!(in >> std::quoted(profile.mission_id) >> std::quoted(profile.frame_id)
            >> profile.loop >> std::quoted(entry.mission_file) >> entry.priority >> entry.sequence) ||
          profile.mission_id.empty() || !ReadCount(in, waypoints) || waypoints == 0) {
        return false;
      }
      for (std::size_t j = 0; j < waypoints; ++j) {
        MissionWaypoint point;
        if (!(in >> point.x >> point.y >> point.yaw) ||
            !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.yaw)) {
          return false;
        }
        profile.waypoints.push_back(point);
      }
      data.queue.push_back(std::move(entry));
    }
    if (!ReadCount(in, count)) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      std::string order;
      if (!(in >> std::quoted(order)) || order.empty() ||
          !data.paused_workflows.insert(std::move(order)).second) {
        return false;
      }
    }
    return true;
  }

  int owner_ = -1;
  std::filesystem::path file_;
  std::string last_saved_;
};

}  // namespace robot_tasks
