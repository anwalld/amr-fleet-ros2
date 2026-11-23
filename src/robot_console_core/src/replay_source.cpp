#include "robot_console_core/replay_source.hpp"

#include <QFile>
#include <QFileInfo>
#include <QList>

namespace robot_console {

ReplaySource::ReplaySource(QObject* parent, MonotonicMsFn clock)
    : DataSource(parent), clock_(clock ? std::move(clock) : defaultMonotonicMs()) {
  timer_.setInterval(10);
  connect(&timer_, &QTimer::timeout, this, &ReplaySource::tick);
}

bool ReplaySource::load(const QString& path, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = QStringLiteral("打不开回放文件 %1：%2").arg(path, f.errorString());
    return false;
  }
  const bool ok = loadFromData(f.readAll(), error);
  if (ok) name_ = QFileInfo(path).fileName();
  return ok;
}

bool ReplaySource::loadFromData(const QByteArray& data, QString* error) {
  records_.clear();
  meta_ = ReplayMeta{};
  first_graph_ = -1;
  skipped_lines_ = 0;
  name_ = QStringLiteral("内存数据");

  const QList<QByteArray> lines = data.split('\n');
  bool have_meta = false;
  quint64 skipped = 0;
  for (const QByteArray& raw : lines) {
    const QByteArray line = raw.trimmed();
    if (line.isEmpty()) continue;
    ReplayRecord rec;
    QString err;
    const ParseResult r = parseRecordLine(line, &rec, &err);
    if (!have_meta) {
      if (r != ParseResult::Ok || rec.kind != RecordKind::Meta) {
        if (error) *error = QStringLiteral("回放文件缺少 meta 头：第一行必须是 kind=meta");
        return false;
      }
      meta_ = std::get<ReplayMeta>(rec.payload);
      if (meta_.v < 1 || meta_.v > kReplayFormatVersion) {
        if (error) {
          *error = QStringLiteral("回放格式版本 v=%1 不受支持（支持 1–%2）")
                       .arg(meta_.v)
                       .arg(kReplayFormatVersion);
        }
        meta_ = ReplayMeta{};
        return false;
      }
      have_meta = true;
      t0_ = rec.t;
      continue;
    }
    if (r != ParseResult::Ok || rec.kind == RecordKind::Meta) {
      ++skipped;
      continue;
    }
    if (rec.kind == RecordKind::Graph && first_graph_ < 0) first_graph_ = records_.size();
    records_.push_back(std::move(rec));
  }
  if (!have_meta) {
    if (error) *error = QStringLiteral("回放文件缺少 meta 头：文件为空");
    return false;
  }
  skipped_lines_ = skipped;
  return true;
}

double ReplaySource::duration() const {
  if (records_.isEmpty()) return 0.0;
  return records_.back().t - t0_;
}

QString ReplaySource::description() const { return QStringLiteral("回放 %1").arg(name_); }

void ReplaySource::start() {
  started_ = true;
  paused_ = false;
  finished_ = false;
  next_ = 0;
  played_s_ = 0.0;
  skip_first_graph_ = false;
  last_ms_ = clock_();
  timer_.start();
  tick();
}

void ReplaySource::stop() {
  timer_.stop();
  started_ = false;
}

void ReplaySource::pause() {
  if (!started_ || paused_) return;
  tick();
  paused_ = true;
}

void ReplaySource::resume() {
  if (!started_ || !paused_) return;
  paused_ = false;
  last_ms_ = clock_();
}

void ReplaySource::setSpeed(double speed) {
  if (speed <= 0.0) return;
  if (started_ && !paused_) tick();
  speed_ = speed;
}

void ReplaySource::restart() {
  next_ = 0;
  played_s_ = 0.0;
  // 循环回到开头：先发第一份路网，播放到它原来的位置时不再重复发出。
  if (first_graph_ >= 0) {
    emitRecord(records_[first_graph_]);
    skip_first_graph_ = true;
  }
}

void ReplaySource::tick() {
  if (!started_ || finished_) return;
  const qint64 now = clock_();
  if (!paused_) played_s_ += static_cast<double>(now - last_ms_) / 1000.0 * speed_;
  last_ms_ = now;
  if (paused_) return;

  while (true) {
    while (next_ < records_.size() && records_[next_].t - t0_ <= played_s_) {
      if (!(skip_first_graph_ && next_ == first_graph_)) emitRecord(records_[next_]);
      ++next_;
    }
    if (next_ < records_.size()) return;
    if (!loop_) {
      finished_ = true;
      timer_.stop();
      emit finished();
      return;
    }
    if (records_.isEmpty() || duration() <= 0.0) {
      finished_ = true;
      timer_.stop();
      emit finished();
      return;
    }
    // 超出文件末尾的部分算进下一圈。
    const double overshoot = played_s_ - duration();
    restart();
    played_s_ = overshoot > 0.0 ? overshoot : 0.0;
  }
}

void ReplaySource::emitRecord(const ReplayRecord& rec) {
  switch (rec.kind) {
    case RecordKind::Graph: emit graphReceived(std::get<StationGraph>(rec.payload)); break;
    case RecordKind::Pose: emit poseReceived(std::get<PoseSample>(rec.payload)); break;
    case RecordKind::Traffic: emit trafficReceived(std::get<TrafficStateData>(rec.payload)); break;
    case RecordKind::Snapshot:
      emit snapshotReceived(std::get<OperatorSnapshotData>(rec.payload));
      break;
    case RecordKind::Event: emit eventReceived(std::get<FleetEvent>(rec.payload)); break;
    case RecordKind::Meta:
    case RecordKind::Unknown: break;
  }
}

}  // namespace robot_console
