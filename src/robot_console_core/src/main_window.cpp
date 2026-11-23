#include "robot_console_core/main_window.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>

#include "robot_console_core/base_map.hpp"
#include "robot_console_core/data_source.hpp"
#include "robot_console_core/fleet_table_model.hpp"
#include "robot_console_core/map_scene.hpp"
#include "robot_console_core/map_view.hpp"
#include "robot_console_core/task_queue_model.hpp"
#include "robot_console_core/traffic_panel.hpp"

namespace robot_console {
namespace {

QTableView* makeTable(QAbstractItemModel* model, QWidget* parent) {
  auto* view = new QTableView(parent);
  view->setModel(model);
  view->verticalHeader()->hide();
  view->horizontalHeader()->setStretchLastSection(true);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  return view;
}

}  // namespace

MainWindow::MainWindow(DataSource* source, MonotonicMsFn clock, QWidget* parent)
    : QMainWindow(parent),
      source_(source),
      clock_(clock ? std::move(clock) : defaultMonotonicMs()),
      scene_(new MapScene(this)),
      fleet_(new FleetTableModel(this)),
      tasks_(new TaskQueueModel(this)) {
  setWindowTitle(QStringLiteral("AMR 调度台"));
  resize(1280, 800);

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  view_ = new MapView(scene_, splitter);
  auto* tabs = new QTabWidget(splitter);
  tabs->addTab(makeTable(fleet_, tabs), QStringLiteral("车队"));
  tabs->addTab(makeTable(tasks_, tabs), QStringLiteral("任务"));
  traffic_panel_ = new TrafficPanel(tabs);
  tabs->addTab(traffic_panel_, QStringLiteral("交通"));
  splitter->addWidget(view_);
  splitter->addWidget(tabs);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  setCentralWidget(splitter);

  source_label_ = new QLabel(this);
  connection_label_ = new QLabel(this);
  mission_label_ = new QLabel(this);
  queue_label_ = new QLabel(this);
  age_label_ = new QLabel(this);
  for (auto* l : {source_label_, connection_label_, mission_label_, queue_label_, age_label_}) {
    statusBar()->addWidget(l);
  }

  connect(source_, &DataSource::graphReceived, scene_, &MapScene::setGraph);
  connect(source_, &DataSource::poseReceived, scene_, &MapScene::setPose);
  connect(source_, &DataSource::trafficReceived, scene_, &MapScene::setTraffic);
  connect(source_, &DataSource::trafficReceived, traffic_panel_, &TrafficPanel::setTraffic);
  connect(source_, &DataSource::snapshotReceived, this, &MainWindow::onSnapshot);
  connect(source_, &DataSource::connectionChanged, this, [this](const ConnectionStatus& s) {
    connection_label_->setText(s.connected ? QStringLiteral("已连接") : QStringLiteral("未连接：%1").arg(s.detail));
  });

  status_timer_.setInterval(500);
  connect(&status_timer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
  status_timer_.start();
  refreshStatus();
}

bool MainWindow::loadBaseMap(const QString& yaml_path, QString* error) {
  BaseMapInfo info;
  if (!robot_console::loadBaseMap(yaml_path, &info, error)) return false;
  scene_->setBaseMap(info);
  return true;
}

void MainWindow::onSnapshot(const OperatorSnapshotData& s) {
  last_snapshot_ms_ = clock_();
  fleet_->setSnapshot(s);
  tasks_->setSnapshot(s);
  mission_label_->setText(QStringLiteral("任务中枢：%1%2")
                              .arg(s.mission_state.isEmpty() ? QStringLiteral("—") : s.mission_state,
                                   s.active_mission_id.isEmpty()
                                       ? QString()
                                       : QStringLiteral("（执行 %1）").arg(s.active_mission_id)));
  queue_label_->setText(QStringLiteral("队列 %1").arg(s.queue_size));
  refreshStatus();
}

void MainWindow::refreshStatus() {
  source_label_->setText(QStringLiteral("数据源：%1").arg(source_->description()));
  if (mission_label_->text().isEmpty()) mission_label_->setText(QStringLiteral("任务中枢：—"));
  if (queue_label_->text().isEmpty()) queue_label_->setText(QStringLiteral("队列 —"));
  if (last_snapshot_ms_ < 0) {
    age_label_->setText(QStringLiteral("尚无快照"));
    return;
  }
  const double age = static_cast<double>(clock_() - last_snapshot_ms_) / 1000.0;
  if (age > kSnapshotStaleS) {
    age_label_->setText(QStringLiteral("快照过期（%1 s）").arg(age, 0, 'f', 1));
    age_label_->setStyleSheet(QStringLiteral("color: #c5221f; font-weight: bold;"));
  } else {
    age_label_->setText(QStringLiteral("快照 %1 s 前").arg(age, 0, 'f', 1));
    age_label_->setStyleSheet(QString());
  }
}

QString MainWindow::statusText() const {
  QStringList parts;
  for (auto* l : {source_label_, connection_label_, mission_label_, queue_label_, age_label_}) {
    if (!l->text().isEmpty()) parts << l->text();
  }
  return parts.join(QStringLiteral(" | "));
}

}  // namespace robot_console
