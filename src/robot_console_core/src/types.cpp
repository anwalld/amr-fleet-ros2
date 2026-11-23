#include "robot_console_core/types.hpp"

#include <QDateTime>
#include <QElapsedTimer>

namespace robot_console {

bool StationGraph::operator==(const StationGraph& o) const {
  return station_ids == o.station_ids && frame_ids == o.frame_ids && x == o.x && y == o.y &&
         yaw == o.yaw && from_station == o.from_station && to_station == o.to_station &&
         distance_m == o.distance_m && bidirectional == o.bidirectional && enabled == o.enabled;
}

MonotonicMsFn defaultMonotonicMs() {
  static QElapsedTimer timer = [] {
    QElapsedTimer t;
    t.start();
    return t;
  }();
  return [] { return timer.elapsed(); };
}

double systemNowSeconds() { return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0; }

void registerMetaTypes() {
  qRegisterMetaType<StationGraph>("robot_console::StationGraph");
  qRegisterMetaType<PoseSample>("robot_console::PoseSample");
  qRegisterMetaType<TrafficStateData>("robot_console::TrafficStateData");
  qRegisterMetaType<OperatorSnapshotData>("robot_console::OperatorSnapshotData");
  qRegisterMetaType<FleetEvent>("robot_console::FleetEvent");
  qRegisterMetaType<ConnectionStatus>("robot_console::ConnectionStatus");
}

}  // namespace robot_console
