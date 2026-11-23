#pragma once
// 冒烟模式：同时收到第一份快照和第一份路网后开始计时，N 秒后写 TSV 并结束（退出码 0）；
// 等待超时仍没收到则不写文件，退出码 2。
#include <QObject>
#include <QString>
#include <QTimer>

#include "robot_console_core/smoke_metrics.hpp"
#include "robot_console_core/types.hpp"

namespace robot_console {

class DataSource;
class MapView;

class SmokeRun : public QObject {
  Q_OBJECT

 public:
  SmokeRun(DataSource* source, MapView* view, double seconds, double wait_seconds, QString out_path,
           QObject* parent = nullptr);

  void start();
  const SmokeMetrics& metrics() const { return metrics_; }
  bool measuring() const { return measuring_; }

 signals:
  void done(int exit_code);

 private:
  void maybeBegin();
  void finish();

  DataSource* source_;
  MapView* view_;
  double seconds_;
  double wait_seconds_;
  QString out_path_;
  SmokeMetrics metrics_;
  MonotonicMsFn clock_ = defaultMonotonicMs();
  qint64 begin_ms_ = 0;
  bool have_graph_ = false;
  bool have_snapshot_ = false;
  bool measuring_ = false;
  bool finished_ = false;
  QTimer wait_timer_;
  QTimer run_timer_;
};

}  // namespace robot_console
