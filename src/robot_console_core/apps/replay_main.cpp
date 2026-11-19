// robot_console_replay：用回放文件驱动调度台界面，白天在 Mac 上开发用。
//   robot_console_replay <回放文件> [--speed 4] [--loop] [--map <地图 yaml>] [--quit-after <秒>]
#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QTimer>

#include "robot_console_core/main_window.hpp"
#include "robot_console_core/replay_source.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("robot_console_replay"));
  robot_console::registerMetaTypes();

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("用回放文件（JSONL）驱动 AMR 调度台界面"));
  parser.addHelpOption();
  parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("回放文件"));
  QCommandLineOption speed(QStringLiteral("speed"), QStringLiteral("倍速（默认 1）"), QStringLiteral("x"), QStringLiteral("1"));
  QCommandLineOption loop(QStringLiteral("loop"), QStringLiteral("循环播放"));
  QCommandLineOption map(QStringLiteral("map"), QStringLiteral("底图 yaml（可选）"), QStringLiteral("yaml"));
  QCommandLineOption quit_after(QStringLiteral("quit-after"), QStringLiteral("N 秒后退出（自动化检查用）"),
                                QStringLiteral("seconds"));
  parser.addOptions({speed, loop, map, quit_after});
  parser.process(app);

  QTextStream err(stderr);
  if (parser.positionalArguments().size() != 1) {
    err << "需要一个回放文件参数\n";
    return 1;
  }
  robot_console::ReplaySource source;
  QString error;
  if (!source.load(parser.positionalArguments().first(), &error)) {
    err << error << '\n';
    return 1;
  }
  source.setSpeed(parser.value(speed).toDouble());
  source.setLoop(parser.isSet(loop));

  robot_console::MainWindow window(&source);
  if (parser.isSet(map) && !window.loadBaseMap(parser.value(map), &error)) {
    err << error << '\n';
    return 1;
  }
  window.show();
  source.start();
  if (source.skippedLines() > 0) err << "跳过 " << source.skippedLines() << " 行（无法解析或不认识的 kind）\n";

  if (parser.isSet(quit_after)) {
    QTimer::singleShot(static_cast<int>(parser.value(quit_after).toDouble() * 1000.0), &app, &QApplication::quit);
  }
  return app.exec();
}
