#pragma once
// 路权资源号解析。格式由 route-reservation 规格规定：route_edge:A__B（两站按字典序排）、route_node:S。
#include <QString>

namespace robot_console {

struct ResourceRef {
  enum class Kind { Invalid, Edge, Node };
  Kind kind = Kind::Invalid;
  QString a;  // Edge：字典序较小的站；Node：站点
  QString b;  // Edge：字典序较大的站
};

ResourceRef parseResource(const QString& resource_id);

// 无序站点对的键，路段着色用。
QString edgeKey(const QString& s1, const QString& s2);

// 占用者是否是封锁（而不是车辆）。
bool isBlockOwner(const QString& owner);

}  // namespace robot_console
