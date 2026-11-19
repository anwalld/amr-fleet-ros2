#include "robot_console_core/smoke_run.hpp"

#include <QTextStream>

#include "robot_console_core/data_source.hpp"
#include "robot_console_core/map_view.hpp"

namespace robot_console {

SmokeRun::SmokeRun(DataSource* source, MapView* view, double seconds, double wait_seconds, QString out_path,
                   QObject* parent)
    : QObject(parent),
      source_(source),
      view_(view),
      seconds_(seconds),
      wait_seconds_(wait_seconds),
      out_path_(std::move(out_path)) {
  wait_timer_.setSingleShot(true);
  run_timer_.setSingleShot(true);
  connect(&wait_timer_, &QTimer::timeout, this, [this] {
    if (measuring_ || finished_) return;
    finished_ = true;
    QTextStream(stderr) << "冒烟：" << wait_seconds_ << " 秒内没有同时收到快照和路网，退出码 2\n";
    emit done(2);
  });
  connect(&run_timer_, &QTimer::timeout, this, &SmokeRun::finish);

  connect(source_, &DataSource::graphReceived, this, [this](const StationGraph&) {
    have_graph_ = true;
    maybeBegin();
  });
  connect(source_, &DataSource::snapshotReceived, this, [this](const OperatorSnapshotData& s) {
    have_snapshot_ = true;
    maybeBegin();
    metrics_.countSnapshot(s.fleet_robot_ids);
  });
  connect(source_, &DataSource::trafficReceived, this, [this](const TrafficStateData&) { metrics_.countTraffic(); });
  connect(source_, &DataSource::eventReceived, this, [this](const FleetEvent&) { metrics_.countEvent(); });
  connect(source_, &DataSource::poseReceived, this, [this](const PoseSample& p) { metrics_.countPose(p.robot); });
}

void SmokeRun::start() { wait_timer_.start(static_cast<int>(wait_seconds_ * 1000.0)); }

void SmokeRun::maybeBegin() {
  if (measuring_ || finished_ || !have_graph_ || !have_snapshot_) return;
  measuring_ = true;
  wait_timer_.stop();
  metrics_.begin();
  begin_ms_ = clock_();
  if (view_) view_->setMetrics(&metrics_);
  run_timer_.start(static_cast<int>(seconds_ * 1000.0));
}

void SmokeRun::finish() {
  if (finished_) return;
  finished_ = true;
  if (view_) view_->setMetrics(nullptr);
  const double duration = static_cast<double>(clock_() - begin_ms_) / 1000.0;
  QString error;
  if (!metrics_.writeTsv(out_path_, duration, source_->serviceTimeouts(), &error)) {
    QTextStream(stderr) << "冒烟：" << error << '\n';
    emit done(1);
    return;
  }
  emit done(0);
}

}  // namespace robot_console
