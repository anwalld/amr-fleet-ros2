// robot_console：连接真实 ROS 系统的 Qt 调度台。
//   ros2 run robot_console robot_console [--map <地图 yaml>]
//       [--smoke-seconds N --smoke-out <TSV> [--smoke-wait-seconds 120]]
//       [--ros-args -p odom_topic_overrides:="['robot_1=/odom']"]
// 冒烟模式（夜跑阶段 7）：同时收到第一份快照和路网后计时 N 秒，写度量 TSV 后以 0 退出；
// 等待超时仍没收到则不写文件，退出码 2。
#include <rclcpp/rclcpp.hpp>

#include <string>
#include <vector>

#include "robot_console/ros_bridge.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/smoke_run.hpp"

int main(int argc, char** argv) {
  // 先把 ROS 参数取走，剩下的交给 Qt。
  std::vector<std::string> args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  std::vector<char*> qt_argv;
  for (auto& a : args) qt_argv.push_back(a.data());
  int qt_argc = static_cast<int>(qt_argv.size());
  qt_argv.push_back(nullptr);

  int rc = 0;
  {
    QApplication app(qt_argc, qt_argv.data());
    QApplication::setApplicationName(QStringLiteral("robot_console"));
    robot_console::registerMetaTypes();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AMR 调度台（连接 ROS）"));
    parser.addHelpOption();
    QCommandLineOption map(QStringLiteral("map"), QStringLiteral("底图 yaml（可选）"), QStringLiteral("yaml"));
    QCommandLineOption smoke_seconds(QStringLiteral("smoke-seconds"), QStringLiteral("冒烟模式：计时秒数"),
                                     QStringLiteral("seconds"));
    QCommandLineOption smoke_out(QStringLiteral("smoke-out"), QStringLiteral("冒烟模式：度量输出 TSV"),
                                 QStringLiteral("file"));
    QCommandLineOption smoke_wait(QStringLiteral("smoke-wait-seconds"),
                                  QStringLiteral("冒烟模式：等待第一份快照和路网的秒数（默认 120）"),
                                  QStringLiteral("seconds"), QStringLiteral("120"));
    parser.addOptions({map, smoke_seconds, smoke_out, smoke_wait});
    parser.process(app);

    QTextStream err(stderr);
    const bool smoke = parser.isSet(smoke_seconds);
    if (smoke && !parser.isSet(smoke_out)) {
      err << "--smoke-seconds 需要同时给 --smoke-out\n";
      rclcpp::shutdown();
      return 1;
    }

    robot_console::RosBridge bridge;
    robot_console::MainWindow window(&bridge, {}, nullptr, bridge.operations());
    QString error;
    if (parser.isSet(map) && !window.loadBaseMap(parser.value(map), &error)) {
      err << error << '\n';
      rclcpp::shutdown();
      return 1;
    }
    window.show();

    std::unique_ptr<robot_console::SmokeRun> run;
    if (smoke) {
      run = std::make_unique<robot_console::SmokeRun>(&bridge, window.mapView(), parser.value(smoke_seconds).toDouble(),
                                                      parser.value(smoke_wait).toDouble(), parser.value(smoke_out));
      QObject::connect(run.get(), &robot_console::SmokeRun::done, &app, [](int code) { QApplication::exit(code); });
      run->start();
    }

    // Ctrl-C / SIGINT 由 rclcpp 处理；ROS 关闭后界面也退出。
    QTimer ros_watch;
    QObject::connect(&ros_watch, &QTimer::timeout, &app, [] {
      if (!rclcpp::ok()) QApplication::exit(130);
    });
    ros_watch.start(200);

    bridge.start();
    rc = app.exec();
    bridge.stop();
  }
  if (rclcpp::ok()) rclcpp::shutdown();
  return rc;
}
