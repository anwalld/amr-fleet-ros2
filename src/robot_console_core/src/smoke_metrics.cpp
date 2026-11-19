#include "robot_console_core/smoke_metrics.hpp"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace robot_console {

void SmokeMetrics::begin() {
  *this = SmokeMetrics{};
  active_ = true;
}

void SmokeMetrics::countSnapshot(const QStringList& fleet_robot_ids) {
  if (!active_) return;
  ++snapshots_;
  fleet_ids_ = fleet_robot_ids;
}

void SmokeMetrics::countTraffic() {
  if (active_) ++traffic_;
}

void SmokeMetrics::countEvent() {
  if (active_) ++events_;
}

void SmokeMetrics::countPose(const QString& robot) {
  if (active_) ++pose_counts_[robot];
}

void SmokeMetrics::addLatencyMs(double ms) {
  if (active_) latency_ms_.push_back(ms);
}

void SmokeMetrics::addLatencySkipped() {
  if (active_) ++latency_skipped_;
}

void SmokeMetrics::addFrame(double paint_ms) {
  if (!active_) return;
  ++frames_;
  paint_ms_.push_back(paint_ms);
}

QStringList SmokeMetrics::robots() const {
  QStringList all = fleet_ids_;
  for (auto it = pose_counts_.cbegin(); it != pose_counts_.cend(); ++it) {
    if (!all.contains(it.key())) all.push_back(it.key());
  }
  all.sort();
  return all;
}

double SmokeMetrics::minPoseHz(double duration_s) const {
  const QStringList all = robots();
  if (all.isEmpty() || duration_s <= 0.0) return 0.0;
  double best = std::numeric_limits<double>::max();
  for (const QString& r : all) best = std::min(best, static_cast<double>(pose_counts_.value(r)) / duration_s);
  return best;
}

int SmokeMetrics::robotsWithPose() const {
  int n = 0;
  for (auto it = pose_counts_.cbegin(); it != pose_counts_.cend(); ++it) n += it.value() > 0 ? 1 : 0;
  return n;
}

double SmokeMetrics::percentile(QVector<double> values, double p) {
  if (values.isEmpty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double rank = std::ceil(p / 100.0 * static_cast<double>(values.size()));
  const int idx = std::clamp(static_cast<int>(rank) - 1, 0, static_cast<int>(values.size()) - 1);
  return values[idx];
}

QVector<QPair<QString, QString>> SmokeMetrics::results(double duration_s, quint64 service_timeouts) const {
  auto f = [](double v, int prec = 2) { return QString::number(v, 'f', prec); };
  auto hz = [&](quint64 n) { return duration_s > 0.0 ? static_cast<double>(n) / duration_s : 0.0; };
  quint64 poses = 0;
  for (auto it = pose_counts_.cbegin(); it != pose_counts_.cend(); ++it) poses += it.value();
  const double max_latency =
      latency_ms_.isEmpty() ? 0.0 : *std::max_element(latency_ms_.cbegin(), latency_ms_.cend());

  QVector<QPair<QString, QString>> rows;
  rows.push_back({QStringLiteral("duration_s"), f(duration_s, 1)});
  rows.push_back({QStringLiteral("snapshots"), QString::number(snapshots_)});
  rows.push_back({QStringLiteral("snapshot_hz"), f(hz(snapshots_))});
  rows.push_back({QStringLiteral("traffic_msgs"), QString::number(traffic_)});
  rows.push_back({QStringLiteral("traffic_hz"), f(hz(traffic_))});
  rows.push_back({QStringLiteral("events"), QString::number(events_)});
  rows.push_back({QStringLiteral("event_hz"), f(hz(events_), 3)});
  rows.push_back({QStringLiteral("robots"), QString::number(robots().size())});
  rows.push_back({QStringLiteral("robots_with_pose"), QString::number(robotsWithPose())});
  rows.push_back({QStringLiteral("pose_updates"), QString::number(poses)});
  rows.push_back({QStringLiteral("pose_hz_min"), f(minPoseHz(duration_s))});
  for (const QString& r : robots()) {
    rows.push_back({QStringLiteral("pose_hz.%1").arg(r), f(hz(pose_counts_.value(r)))});
  }
  rows.push_back({QStringLiteral("service_timeouts"), QString::number(service_timeouts)});
  rows.push_back({QStringLiteral("latency_samples"), QString::number(latency_ms_.size())});
  rows.push_back({QStringLiteral("latency_p50_ms"), f(percentile(latency_ms_, 50))});
  rows.push_back({QStringLiteral("latency_p95_ms"), f(percentile(latency_ms_, 95))});
  rows.push_back({QStringLiteral("latency_max_ms"), f(max_latency)});
  rows.push_back({QStringLiteral("latency_skipped"), QString::number(latency_skipped_)});
  rows.push_back({QStringLiteral("frames"), QString::number(frames_)});
  rows.push_back({QStringLiteral("fps"), f(hz(frames_))});
  rows.push_back({QStringLiteral("paint_p95_ms"), f(percentile(paint_ms_, 95), 3)});
  return rows;
}

bool SmokeMetrics::writeTsv(const QString& path, double duration_s, quint64 service_timeouts,
                            QString* error) const {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    if (error) *error = QStringLiteral("写不了 %1：%2").arg(path, f.errorString());
    return false;
  }
  QTextStream out(&f);
  out << "metric\tvalue\n";
  for (const auto& row : results(duration_s, service_timeouts)) out << row.first << '\t' << row.second << '\n';
  return true;
}

}  // namespace robot_console
