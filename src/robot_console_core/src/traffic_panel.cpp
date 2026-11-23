#include "robot_console_core/traffic_panel.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>

#include "robot_console_core/traffic_models.hpp"

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

TrafficPanel::TrafficPanel(QWidget* parent)
    : QWidget(parent), occupancy_(new OccupancyModel(this)), waits_(new WaitQueueModel(this)) {
  auto* layout = new QVBoxLayout(this);

  auto* deadlock_box = new QGroupBox(QStringLiteral("死锁"), this);
  auto* form = new QFormLayout(deadlock_box);
  addField(form, QStringLiteral("当前环上的车"), QStringLiteral("currentRing"));
  addField(form, QStringLiteral("累计检测次数"), QStringLiteral("checkCount"));
  addField(form, QStringLiteral("检出次数"), QStringLiteral("deadlockCount"));
  addField(form, QStringLiteral("解开次数"), QStringLiteral("resolvedCount"));
  addField(form, QStringLiteral("改道 / 退避 / 暂停"), QStringLiteral("actionCounts"));
  addField(form, QStringLiteral("最近一次解除"), QStringLiteral("lastResolution"));
  addField(form, QStringLiteral("最近一次解除耗时"), QStringLiteral("lastResolutionMs"));
  addField(form, QStringLiteral("单次检测耗时（最近 / 最大）"), QStringLiteral("detectionUs"));
  layout->addWidget(deadlock_box);

  layout->addWidget(new QLabel(QStringLiteral("占用表"), this));
  layout->addWidget(makeTable(occupancy_, this), 1);
  layout->addWidget(new QLabel(QStringLiteral("等待队列"), this));
  layout->addWidget(makeTable(waits_, this), 1);
}

QLabel* TrafficPanel::addField(QFormLayout* form, const QString& title, const QString& name) {
  auto* label = new QLabel(QStringLiteral("—"), this);
  label->setObjectName(name);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(title, label);
  return label;
}

QString TrafficPanel::formatResolutionMs(double ms) {
  if (ms < 0.0) return QStringLiteral("尚未解开");
  return QStringLiteral("%1 ms").arg(ms, 0, 'f', 1);
}

void TrafficPanel::setTraffic(const TrafficStateData& t) {
  occupancy_->setTraffic(t);
  waits_->setTraffic(t);
  auto set = [this](const char* name, const QString& text) {
    if (auto* l = findChild<QLabel*>(QLatin1String(name))) l->setText(text);
  };
  set("currentRing", t.deadlock_robot_ids.isEmpty() ? QStringLiteral("无") : t.deadlock_robot_ids.join(QStringLiteral(", ")));
  set("checkCount", QString::number(t.deadlock_check_count));
  set("deadlockCount", QString::number(t.deadlock_count));
  set("resolvedCount", QString::number(t.resolved_count));
  set("actionCounts", QStringLiteral("%1 / %2 / %3").arg(t.reroute_count).arg(t.backoff_count).arg(t.pause_count));
  set("lastResolution", t.last_resolution_action.isEmpty()
                            ? QStringLiteral("无")
                            : QStringLiteral("%1（让路车 %2）").arg(t.last_resolution_action, t.last_resolution_robot_id));
  set("lastResolutionMs", t.last_resolution_action.isEmpty() && t.last_resolution_ms < 0.0
                              ? QStringLiteral("尚未解开")
                              : formatResolutionMs(t.last_resolution_ms));
  set("detectionUs", QStringLiteral("%1 / %2 µs").arg(t.last_detection_us, 0, 'f', 1).arg(t.max_detection_us, 0, 'f', 1));
}

QString TrafficPanel::fieldText(const QString& name) const {
  auto* l = findChild<QLabel*>(name);
  return l ? l->text() : QString();
}

}  // namespace robot_console
