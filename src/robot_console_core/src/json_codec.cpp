#include "robot_console_core/json_codec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <initializer_list>

namespace robot_console {
namespace {

QStringList strings(const QJsonObject& o, const char* key) {
  QStringList out;
  const QJsonArray a = o.value(QLatin1String(key)).toArray();
  out.reserve(a.size());
  for (const auto& v : a) out.push_back(v.toString());
  return out;
}

QVector<double> doubles(const QJsonObject& o, const char* key) {
  QVector<double> out;
  const QJsonArray a = o.value(QLatin1String(key)).toArray();
  out.reserve(a.size());
  for (const auto& v : a) out.push_back(v.toDouble());
  return out;
}

QVector<int> ints(const QJsonObject& o, const char* key) {
  QVector<int> out;
  const QJsonArray a = o.value(QLatin1String(key)).toArray();
  out.reserve(a.size());
  for (const auto& v : a) out.push_back(static_cast<int>(v.toInteger()));
  return out;
}

QVector<bool> bools(const QJsonObject& o, const char* key) {
  QVector<bool> out;
  const QJsonArray a = o.value(QLatin1String(key)).toArray();
  out.reserve(a.size());
  for (const auto& v : a) out.push_back(v.toBool());
  return out;
}

QString str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }
double num(const QJsonObject& o, const char* key, double def = 0.0) {
  return o.value(QLatin1String(key)).toDouble(def);
}
quint64 u64(const QJsonObject& o, const char* key) {
  return static_cast<quint64>(o.value(QLatin1String(key)).toInteger());
}
bool flag(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toBool(); }

// builtin_interfaces/Time 导出为 {"sec":..,"nanosec":..}；也接受秒数。
double stampSeconds(const QJsonValue& v) {
  if (v.isObject()) {
    const QJsonObject o = v.toObject();
    return static_cast<double>(o.value(QLatin1String("sec")).toInteger()) +
           static_cast<double>(o.value(QLatin1String("nanosec")).toInteger()) * 1e-9;
  }
  return v.toDouble();
}

bool hasFields(const QJsonObject& object, QJsonValue::Type type,
               std::initializer_list<const char*> fields) {
  for (const char* field : fields) {
    if (object.value(QLatin1String(field)).type() != type) return false;
  }
  return true;
}

bool hasStamp(const QJsonValue& stamp) {
  return stamp.isDouble() || hasFields(stamp.toObject(), QJsonValue::Double, {"sec", "nanosec"});
}

RecordKind kindFromName(const QString& name) {
  if (name == QLatin1String("meta")) return RecordKind::Meta;
  if (name == QLatin1String("graph")) return RecordKind::Graph;
  if (name == QLatin1String("pose")) return RecordKind::Pose;
  if (name == QLatin1String("traffic")) return RecordKind::Traffic;
  if (name == QLatin1String("snapshot")) return RecordKind::Snapshot;
  if (name == QLatin1String("event")) return RecordKind::Event;
  return RecordKind::Unknown;
}

}  // namespace

ReplayMeta metaFromJson(const QJsonObject& rec) {
  ReplayMeta m;
  m.v = static_cast<int>(rec.value(QLatin1String("v")).toInteger());
  m.network = str(rec, "network");
  m.robots = static_cast<int>(rec.value(QLatin1String("robots")).toInteger());
  m.mode = str(rec, "mode");
  m.rev = str(rec, "rev");
  m.source = str(rec, "source");
  m.raw = rec;
  return m;
}

StationGraph graphFromJson(const QJsonObject& rec) {
  StationGraph g;
  const QJsonObject st = rec.value(QLatin1String("stations")).toObject();
  const QJsonObject rt = rec.value(QLatin1String("routes")).toObject();
  g.station_ids = strings(st, "station_ids");
  g.frame_ids = strings(st, "frame_ids");
  g.x = doubles(st, "x");
  g.y = doubles(st, "y");
  g.yaw = doubles(st, "yaw");
  g.from_station = strings(rt, "from_station");
  g.to_station = strings(rt, "to_station");
  g.distance_m = doubles(rt, "distance_m");
  g.bidirectional = bools(rt, "bidirectional");
  g.enabled = bools(rt, "enabled");
  return g;
}

PoseSample poseFromJson(const QJsonObject& rec) {
  PoseSample p;
  p.robot = str(rec, "robot");
  p.stamp = stampSeconds(rec.value(QLatin1String("stamp")));
  p.x = num(rec, "x");
  p.y = num(rec, "y");
  p.yaw = num(rec, "yaw");
  p.vx = num(rec, "vx");
  p.wz = num(rec, "wz");
  return p;
}

TrafficStateData trafficFromJson(const QJsonObject& d) {
  TrafficStateData s;
  s.stamp = stampSeconds(d.value(QLatin1String("stamp")));
  s.mode = str(d, "mode");
  s.resource_ids = strings(d, "resource_ids");
  s.owner_ids = strings(d, "owner_ids");
  s.waiting_resource_ids = strings(d, "waiting_resource_ids");
  s.waiting_robot_ids = strings(d, "waiting_robot_ids");
  s.waiting_priorities = ints(d, "waiting_priorities");
  s.waiting_positions = ints(d, "waiting_positions");
  s.grant_count = u64(d, "grant_count");
  s.queue_count = u64(d, "queue_count");
  s.deadlock_check_count = u64(d, "deadlock_check_count");
  s.deadlock_count = u64(d, "deadlock_count");
  s.deadlock_robot_ids = strings(d, "deadlock_robot_ids");
  s.last_resolution_action = str(d, "last_resolution_action");
  s.last_resolution_robot_id = str(d, "last_resolution_robot_id");
  s.last_resolution_ms = num(d, "last_resolution_ms", -1.0);
  s.resolved_count = u64(d, "resolved_count");
  s.last_detection_us = num(d, "last_detection_us");
  s.max_detection_us = num(d, "max_detection_us");
  s.reroute_count = u64(d, "reroute_count");
  s.backoff_count = u64(d, "backoff_count");
  s.pause_count = u64(d, "pause_count");
  return s;
}

OperatorSnapshotData snapshotFromJson(const QJsonObject& d) {
  OperatorSnapshotData s;
  s.success = flag(d, "success");
  s.message = str(d, "message");
  s.mission_state = str(d, "mission_state");
  s.previous_mission_state = str(d, "previous_mission_state");
  s.active_mission_id = str(d, "active_mission_id");
  s.mission_message = str(d, "mission_message");
  s.recoverable = flag(d, "recoverable");
  s.mission_active = flag(d, "mission_active");
  s.paused = flag(d, "paused");
  s.queue_size = static_cast<int>(d.value(QLatin1String("queue_size")).toInteger());
  s.queued_mission_ids = strings(d, "queued_mission_ids");
  s.queued_priorities = ints(d, "queued_priorities");
  s.paused_workflow_order_ids = strings(d, "paused_workflow_order_ids");
  s.fleet_robot_ids = strings(d, "fleet_robot_ids");
  s.fleet_enabled = bools(d, "fleet_enabled");
  s.fleet_states = strings(d, "fleet_states");
  s.fleet_battery_voltage = doubles(d, "fleet_battery_voltage");
  s.fleet_current_station_ids = strings(d, "fleet_current_station_ids");
  s.fleet_capabilities = strings(d, "fleet_capabilities");
  s.remote_task_ids = strings(d, "remote_task_ids");
  s.remote_task_robot_ids = strings(d, "remote_task_robot_ids");
  s.remote_task_states = strings(d, "remote_task_states");
  s.facility_resource_ids = strings(d, "facility_resource_ids");
  s.facility_resource_types = strings(d, "facility_resource_types");
  s.facility_station_ids = strings(d, "facility_station_ids");
  s.facility_available = bools(d, "facility_available");
  s.facility_status = strings(d, "facility_status");
  s.business_types = strings(d, "business_types");
  s.business_scenario_ids = strings(d, "business_scenario_ids");
  s.business_workflow_ids = strings(d, "business_workflow_ids");
  s.recent_event_stamp = strings(d, "recent_event_stamp");
  s.recent_event_mission_ids = strings(d, "recent_event_mission_ids");
  s.recent_event_states = strings(d, "recent_event_states");
  s.recent_event_messages = strings(d, "recent_event_messages");
  return s;
}

ParseResult parseRecordLine(const QByteArray& line, ReplayRecord* out, QString* error) {
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) *error = QStringLiteral("JSON 解析失败：%1").arg(perr.errorString());
    return ParseResult::Invalid;
  }
  const QJsonObject rec = doc.object();
  if (!rec.contains(QLatin1String("t")) || !rec.value(QLatin1String("t")).isDouble() ||
      !rec.value(QLatin1String("kind")).isString()) {
    if (error) *error = QStringLiteral("缺少 t 或 kind");
    return ParseResult::Invalid;
  }
  out->t = rec.value(QLatin1String("t")).toDouble();
  out->kind_name = rec.value(QLatin1String("kind")).toString();
  out->kind = kindFromName(out->kind_name);
  const QJsonObject data = rec.value(QLatin1String("data")).toObject();
  switch (out->kind) {
    case RecordKind::Meta: out->payload = metaFromJson(rec); break;
    case RecordKind::Graph:
      if (!hasFields(rec.value(QLatin1String("stations")).toObject(), QJsonValue::Array,
                     {"station_ids", "x", "y"}) ||
          !hasFields(rec.value(QLatin1String("routes")).toObject(), QJsonValue::Array,
                     {"from_station", "to_station", "bidirectional", "enabled"})) {
        if (error) *error = QStringLiteral("路网缺少站点或路段字段");
        return ParseResult::Invalid;
      }
      out->payload = graphFromJson(rec);
      break;
    case RecordKind::Pose:
      if (str(rec, "robot").isEmpty() || !hasStamp(rec.value(QLatin1String("stamp"))) ||
          !hasFields(rec, QJsonValue::Double, {"x", "y", "yaw"})) {
        if (error) *error = QStringLiteral("位置记录缺少车辆、时间戳或位姿");
        return ParseResult::Invalid;
      }
      out->payload = poseFromJson(rec);
      break;
    case RecordKind::Traffic:
      if (!hasFields(data, QJsonValue::Array,
                     {"resource_ids", "owner_ids", "waiting_resource_ids", "waiting_robot_ids",
                      "deadlock_robot_ids"}) ||
          !hasFields(data, QJsonValue::Double, {"resolved_count"})) {
        if (error) *error = QStringLiteral("交通状态缺少占用、等待或死锁字段");
        return ParseResult::Invalid;
      }
      out->payload = trafficFromJson(data);
      break;
    case RecordKind::Snapshot:
      if (!hasFields(data, QJsonValue::String, {"mission_state"}) ||
          !hasFields(data, QJsonValue::Array, {"fleet_robot_ids", "queued_mission_ids", "remote_task_ids"})) {
        if (error) *error = QStringLiteral("快照缺少任务或车队字段");
        return ParseResult::Invalid;
      }
      out->payload = snapshotFromJson(data);
      break;
    case RecordKind::Event:
      if (!rec.value(QLatin1String("data")).isObject()) {
        if (error) *error = QStringLiteral("车队事件缺少 data 对象");
        return ParseResult::Invalid;
      }
      out->payload = FleetEvent{data};
      break;
    case RecordKind::Unknown:
      out->payload = std::monostate{};
      return ParseResult::UnknownKind;
  }
  return ParseResult::Ok;
}

}  // namespace robot_console
