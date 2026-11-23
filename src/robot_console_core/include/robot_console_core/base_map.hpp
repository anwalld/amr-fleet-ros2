#pragma once
// 可选底图：ROS 地图 yaml（image、resolution、origin）加图像（PGM / PNG）。
#include <QImage>
#include <QString>

namespace robot_console {

struct BaseMapInfo {
  QImage image;
  double resolution = 0.05;  // 米 / 像素
  double origin_x = 0.0;     // 图像左下角像素的地图坐标
  double origin_y = 0.0;
};

bool loadBaseMap(const QString& yaml_path, BaseMapInfo* out, QString* error);

}  // namespace robot_console
