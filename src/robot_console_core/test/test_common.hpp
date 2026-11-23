#pragma once
// 测试公用：读手写的 mini.jsonl、可拨动的假时钟。
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>

#include <memory>

#include "robot_console_core/json_codec.hpp"
#include "robot_console_core/types.hpp"

namespace robot_console::test {

inline QString dataPath(const QString& name) {
  return QStringLiteral(ROBOT_CONSOLE_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

inline QByteArray readData(const QString& name) {
  QFile f(dataPath(name));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

// mini.jsonl 中第一条指定 kind 的记录。
inline ReplayRecord miniRecord(RecordKind kind) {
  for (const QByteArray& line : readData(QStringLiteral("mini.jsonl")).split('\n')) {
    if (line.trimmed().isEmpty()) continue;
    ReplayRecord rec;
    if (parseRecordLine(line, &rec) == ParseResult::Ok && rec.kind == kind) return rec;
  }
  return {};
}

inline StationGraph miniGraph() { return std::get<StationGraph>(miniRecord(RecordKind::Graph).payload); }
inline OperatorSnapshotData miniSnapshot() {
  return std::get<OperatorSnapshotData>(miniRecord(RecordKind::Snapshot).payload);
}
inline TrafficStateData miniTraffic() { return std::get<TrafficStateData>(miniRecord(RecordKind::Traffic).payload); }

struct FakeClock {
  std::shared_ptr<qint64> ms = std::make_shared<qint64>(0);
  MonotonicMsFn fn() const {
    auto p = ms;
    return [p] { return *p; };
  }
  void advance(qint64 d) { *ms += d; }
};

}  // namespace robot_console::test
