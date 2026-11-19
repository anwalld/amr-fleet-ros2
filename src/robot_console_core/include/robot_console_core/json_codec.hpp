#pragma once
// 回放文件（JSONL）一行到结构体的解析。格式见 openspec 规格 operator-console「回放文件格式」。
#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <variant>

#include "robot_console_core/types.hpp"

namespace robot_console {

constexpr int kReplayFormatVersion = 1;

enum class RecordKind { Meta, Graph, Pose, Traffic, Snapshot, Event, Unknown };

struct ReplayRecord {
  double t = 0.0;
  RecordKind kind = RecordKind::Unknown;
  QString kind_name;
  std::variant<std::monostate, ReplayMeta, StationGraph, PoseSample, TrafficStateData,
               OperatorSnapshotData, FleetEvent>
      payload;
};

enum class ParseResult { Ok, UnknownKind, Invalid };

// 解析一行。Invalid 时 error 写原因。
ParseResult parseRecordLine(const QByteArray& line, ReplayRecord* out, QString* error = nullptr);

ReplayMeta metaFromJson(const QJsonObject& rec);
StationGraph graphFromJson(const QJsonObject& rec);  // rec 含 stations、routes
PoseSample poseFromJson(const QJsonObject& rec);
TrafficStateData trafficFromJson(const QJsonObject& data);
OperatorSnapshotData snapshotFromJson(const QJsonObject& data);

}  // namespace robot_console
