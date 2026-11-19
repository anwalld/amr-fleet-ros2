#include "robot_console/ros_bridge.hpp"

#include <chrono>

namespace robot_console {

using namespace std::chrono_literals;

RosBridge::RosBridge(QObject* parent) : DataSource(parent) {}

RosBridge::~RosBridge() { stop(); }

std::map<std::string, std::string> RosBridge::parseOdomOverrides(const std::vector<std::string>& items) {
  std::map<std::string, std::string> out;
  for (const auto& item : items) {
    const auto eq = item.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= item.size()) continue;
    out[item.substr(0, eq)] = item.substr(eq + 1);
  }
  return out;
}

double RosBridge::nowS() const {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void RosBridge::start() {
  if (node_) return;
  node_ = std::make_shared<rclcpp::Node>("robot_console");
  const auto overrides =
      node_->declare_parameter<std::vector<std::string>>("odom_topic_overrides", std::vector<std::string>{});
  odom_overrides_ = parseOdomOverrides(overrides);
  node_->declare_parameter<int64_t>("snapshot_event_limit", 20);

  snapshot_client_ = node_->create_client<GetSnapshot>("/v2/get_operator_snapshot");
  stations_client_ = node_->create_client<ListStations>("/v2/list_stations");
  routes_client_ = node_->create_client<ListRoutes>("/v2/list_station_routes");

  traffic_sub_ = node_->create_subscription<robot_interfaces_mission::msg::TrafficState>(
      "/traffic/state", rclcpp::QoS(10).reliable(),
      [this](robot_interfaces_mission::msg::TrafficState::ConstSharedPtr msg) {
        emit trafficReceived(trafficFromRos(*msg));
      });
  events_sub_ = node_->create_subscription<std_msgs::msg::String>(
      "/fleet/agent_events", rclcpp::QoS(100).reliable(),
      [this](std_msgs::msg::String::ConstSharedPtr msg) { emit eventReceived(eventFromRos(*msg)); });

  snapshot_timer_ = node_->create_wall_timer(1s, [this] { pollSnapshot(); });
  graph_timer_ = node_->create_wall_timer(5s, [this] { pollGraph(); });
  timeout_timer_ = node_->create_wall_timer(500ms, [this] { checkTimeouts(); });
  // 启动后尽快取一次路网，不用等第一个 5 秒。
  auto first_graph = std::make_shared<rclcpp::TimerBase::SharedPtr>();
  *first_graph = node_->create_wall_timer(300ms, [this, first_graph] {
    pollGraph();
    (*first_graph)->cancel();
  });

  executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  executor_->add_node(node_);
  thread_ = QThread::create([exec = executor_] { exec->spin(); });
  thread_->setObjectName(QStringLiteral("ros_executor"));
  thread_->start();
  RCLCPP_INFO(node_->get_logger(), "robot_console 已连接 ROS（只读），话题覆盖 %zu 项", odom_overrides_.size());
}

void RosBridge::stop() {
  if (!executor_) return;
  executor_->cancel();
  if (thread_) {
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
  }
  executor_->remove_node(node_);
  executor_.reset();
  odom_subs_.clear();
  traffic_sub_.reset();
  events_sub_.reset();
  snapshot_timer_.reset();
  graph_timer_.reset();
  timeout_timer_.reset();
  snapshot_client_.reset();
  stations_client_.reset();
  routes_client_.reset();
  node_.reset();
}

void RosBridge::setConnected(bool connected, const QString& detail) {
  if (connection_known_ && connected == connected_) return;
  connection_known_ = true;
  connected_ = connected;
  emit connectionChanged(ConnectionStatus{connected, detail});
}

void RosBridge::pollSnapshot() {
  if (!snapshot_tracker_.canSend()) return;
  if (!snapshot_client_->service_is_ready()) {
    setConnected(false, QStringLiteral("等待 /v2/get_operator_snapshot"));
    return;
  }
  auto req = std::make_shared<GetSnapshot::Request>();
  req->event_limit = static_cast<int32_t>(node_->get_parameter("snapshot_event_limit").as_int());
  auto id = std::make_shared<int64_t>(-1);
  auto sent = snapshot_client_->async_send_request(req, [this, id](rclcpp::Client<GetSnapshot>::SharedFuture f) {
    snapshot_tracker_.markDone(*id);
    // 超时后才到的迟到应答也是有效数据，照常交给界面。
    onSnapshot(*f.get());
  });
  *id = sent.request_id;
  snapshot_tracker_.markSent(*id, nowS());
}

void RosBridge::onSnapshot(const GetSnapshot::Response& res) {
  setConnected(true, QString());
  ensureOdomSubscriptions(res.fleet_robot_ids);
  emit snapshotReceived(snapshotFromRos(res));
}

void RosBridge::pollGraph() {
  if (!stations_tracker_.canSend() || !routes_tracker_.canSend()) return;
  if (!stations_client_->service_is_ready() || !routes_client_->service_is_ready()) return;
  pending_stations_.reset();
  pending_routes_.reset();
  auto sid = std::make_shared<int64_t>(-1);
  auto s = stations_client_->async_send_request(std::make_shared<ListStations::Request>(),
                                                [this, sid](rclcpp::Client<ListStations>::SharedFuture f) {
                                                  if (!stations_tracker_.markDone(*sid)) return;
                                                  pending_stations_ = *f.get();
                                                  maybeEmitGraph();
                                                });
  *sid = s.request_id;
  stations_tracker_.markSent(*sid, nowS());
  auto rid = std::make_shared<int64_t>(-1);
  auto r = routes_client_->async_send_request(std::make_shared<ListRoutes::Request>(),
                                              [this, rid](rclcpp::Client<ListRoutes>::SharedFuture f) {
                                                if (!routes_tracker_.markDone(*rid)) return;
                                                pending_routes_ = *f.get();
                                                maybeEmitGraph();
                                              });
  *rid = r.request_id;
  routes_tracker_.markSent(*rid, nowS());
}

void RosBridge::maybeEmitGraph() {
  if (!pending_stations_ || !pending_routes_) return;
  if (pending_stations_->success && pending_routes_->success) {
    StationGraph g = graphFromRos(*pending_stations_, *pending_routes_);
    if (!last_graph_ || *last_graph_ != g) {
      last_graph_ = g;
      emit graphReceived(g);
    }
  }
  pending_stations_.reset();
  pending_routes_.reset();
}

void RosBridge::checkTimeouts() {
  const double now = nowS();
  if (auto id = snapshot_tracker_.checkTimeout(now)) {
    snapshot_client_->remove_pending_request(*id);
    ++service_timeouts_;
    setConnected(false, QStringLiteral("快照服务 3 秒未应答"));
  }
  if (auto id = stations_tracker_.checkTimeout(now)) {
    stations_client_->remove_pending_request(*id);
    ++service_timeouts_;
  }
  if (auto id = routes_tracker_.checkTimeout(now)) {
    routes_client_->remove_pending_request(*id);
    ++service_timeouts_;
  }
}

void RosBridge::ensureOdomSubscriptions(const std::vector<std::string>& robot_ids) {
  for (const auto& id : robot_ids) {
    if (odom_subs_.count(id)) continue;
    const auto ov = odom_overrides_.find(id);
    const std::string topic = ov != odom_overrides_.end() ? ov->second : "/" + id + "/odom";
    const QString robot = QString::fromStdString(id);
    // best effort 能同时接上 reliable 和 best effort 的发布者。
    odom_subs_[id] = node_->create_subscription<nav_msgs::msg::Odometry>(
        topic, rclcpp::SensorDataQoS(), [this, robot](nav_msgs::msg::Odometry::ConstSharedPtr msg) {
          emit poseReceived(poseFromOdom(robot, *msg));
        });
    RCLCPP_INFO(node_->get_logger(), "订阅 %s 的位置：%s", id.c_str(), topic.c_str());
  }
}

}  // namespace robot_console
