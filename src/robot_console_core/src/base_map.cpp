#include "robot_console_core/base_map.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace robot_console {

bool loadBaseMap(const QString& yaml_path, BaseMapInfo* out, QString* error) {
  QFile f(yaml_path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) *error = QStringLiteral("打不开地图 yaml %1").arg(yaml_path);
    return false;
  }
  QString image;
  bool have_res = false;
  bool have_origin = false;
  BaseMapInfo info;
  // 只读需要的三个键，不引入 yaml 库。
  static const QRegularExpression kv(QStringLiteral("^\\s*(\\w+)\\s*:\\s*(.*?)\\s*(#.*)?$"));
  static const QRegularExpression num(QStringLiteral("[-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?"));
  QTextStream in(&f);
  while (!in.atEnd()) {
    const auto m = kv.match(in.readLine());
    if (!m.hasMatch()) continue;
    const QString key = m.captured(1);
    QString value = m.captured(2);
    if (key == QLatin1String("image")) {
      value.remove(QLatin1Char('"'));
      value.remove(QLatin1Char('\''));
      image = value.trimmed();
    } else if (key == QLatin1String("resolution")) {
      info.resolution = value.toDouble(&have_res);
    } else if (key == QLatin1String("origin")) {
      auto it = num.globalMatch(value);
      QVector<double> vals;
      while (it.hasNext()) vals.push_back(it.next().captured(0).toDouble());
      if (vals.size() >= 2) {
        info.origin_x = vals[0];
        info.origin_y = vals[1];
        have_origin = true;
      }
    }
  }
  if (image.isEmpty() || !have_res || info.resolution <= 0.0 || !have_origin) {
    if (error) *error = QStringLiteral("地图 yaml 缺少 image、resolution 或 origin");
    return false;
  }
  const QString image_path = QFileInfo(image).isAbsolute()
                                 ? image
                                 : QFileInfo(yaml_path).absoluteDir().filePath(image);
  if (!info.image.load(image_path)) {
    if (error) *error = QStringLiteral("读不了底图图像 %1").arg(image_path);
    return false;
  }
  *out = info;
  return true;
}

}  // namespace robot_console
