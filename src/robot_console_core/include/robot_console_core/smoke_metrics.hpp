#pragma once
// 冒烟度量：条数、频率、位置延迟、帧率、单帧绘制耗时。口径见规格 operator-console「冒烟度量」。
#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace robot_console {

class SmokeMetrics {
 public:
  // 开始计数；此前收到的数据不计。
  void begin();
  bool active() const { return active_; }

  void countSnapshot(const QStringList& fleet_robot_ids);
  void countTraffic();
  void countEvent();
  void countPose(const QString& robot);
  void addLatencyMs(double ms);
  void addLatencySkipped();
  void addFrame(double paint_ms);

  quint64 snapshots() const { return snapshots_; }
  quint64 trafficMsgs() const { return traffic_; }
  quint64 events() const { return events_; }
  quint64 frames() const { return frames_; }
  quint64 latencySkipped() const { return latency_skipped_; }
  const QVector<double>& latencySamples() const { return latency_ms_; }
  quint64 poseCount(const QString& robot) const { return pose_counts_.value(robot); }

  // 最近一份快照里的车辆与收到过位置的车辆的并集中，每车位置频率的最低值。
  double minPoseHz(double duration_s) const;
  int robotsWithPose() const;
  QStringList robots() const;

  // 两列（metric、value）的行。
  QVector<QPair<QString, QString>> results(double duration_s, quint64 service_timeouts) const;
  bool writeTsv(const QString& path, double duration_s, quint64 service_timeouts, QString* error) const;

  // 最近秩法分位数，p 取 0–100；空数组返回 0。
  static double percentile(QVector<double> values, double p);

 private:
  bool active_ = false;
  quint64 snapshots_ = 0;
  quint64 traffic_ = 0;
  quint64 events_ = 0;
  quint64 frames_ = 0;
  quint64 latency_skipped_ = 0;
  QStringList fleet_ids_;
  QHash<QString, quint64> pose_counts_;
  QVector<double> latency_ms_;
  QVector<double> paint_ms_;
};

}  // namespace robot_console
