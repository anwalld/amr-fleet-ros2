#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace robot_tasks {

struct Station {
  std::string id;
  std::string frame_id = "map";
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
  // M3：是否避让点（站点字段 yield_point）。未写时按编号是否以 park_ 开头判断。
  std::optional<bool> yield_point;
};

struct StationEdge {
  std::string from;
  std::string to;
  double distance_m = 0.0;
  bool bidirectional = true;
  bool enabled = true;
};

struct StationCatalog {
  std::vector<Station> stations;
  std::vector<StationEdge> edges;
};

struct StationListProjection {
  std::vector<std::string> station_ids;
  std::vector<std::string> frame_ids;
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> yaw;
  std::string message;
};

struct StationRouteListProjection {
  std::vector<std::string> from_station;
  std::vector<std::string> to_station;
  std::vector<double> distance_m;
  std::vector<bool> bidirectional;
  std::vector<bool> enabled;
  std::string message;
};

std::optional<StationCatalog> LoadStationCatalog(const std::filesystem::path& path);
const Station* FindStation(const StationCatalog& catalog, const std::string& id);
// 站点是否避让点：显式 yield_point 优先，否则编号以 park_ 开头即是。
bool IsYieldPoint(const Station& station);
std::optional<double> EstimateRouteDistance(
    const StationCatalog& catalog, const std::string& from, const std::string& to);
std::optional<std::vector<std::string>> FindRoutePath(
    const StationCatalog& catalog, const std::string& from, const std::string& to);
bool ValidateStationCatalog(const StationCatalog& catalog, std::string* message);
StationListProjection BuildStationListProjection(const StationCatalog& catalog);
StationRouteListProjection BuildStationRouteListProjection(const StationCatalog& catalog);

}  // namespace robot_tasks
