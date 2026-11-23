#pragma once
// 读回放文件（JSONL），按记录的 t 节奏发出数据。支持暂停、继续、倍速、循环。
#include <QTimer>
#include <QVector>

#include "robot_console_core/data_source.hpp"
#include "robot_console_core/json_codec.hpp"

namespace robot_console {

class ReplaySource : public DataSource {
  Q_OBJECT

 public:
  explicit ReplaySource(QObject* parent = nullptr, MonotonicMsFn clock = {});

  // 加载并校验。失败时 error 写原因，回放源保持空。
  bool load(const QString& path, QString* error);
  bool loadFromData(const QByteArray& data, QString* error);

  const ReplayMeta& meta() const { return meta_; }
  int recordCount() const { return records_.size(); }
  double duration() const;  // 最后一条与第一条记录的 t 之差（秒）

  QString description() const override;
  void start() override;  // 从头播放
  void stop() override;
  void pause();
  void resume();
  bool isPaused() const { return paused_; }
  void setSpeed(double speed);
  double speed() const { return speed_; }
  void setLoop(bool loop) { loop_ = loop; }
  bool isFinished() const { return finished_; }

  // 发出所有到期的记录。定时器每 10 ms 调用一次；测试中配合注入的时钟直接调用。
  void tick();

 signals:
  void finished();

 private:
  void emitRecord(const ReplayRecord& rec);
  void restart();

  MonotonicMsFn clock_;
  QString name_;
  ReplayMeta meta_;
  QVector<ReplayRecord> records_;  // 不含 meta
  int first_graph_ = -1;
  int next_ = 0;
  double t0_ = 0.0;
  double played_s_ = 0.0;
  qint64 last_ms_ = 0;
  bool started_ = false;
  bool paused_ = false;
  bool finished_ = false;
  bool loop_ = false;
  bool skip_first_graph_ = false;
  double speed_ = 1.0;
  QTimer timer_;
};

}  // namespace robot_console
