#include "robot_console_core/resource_id.hpp"

#include <algorithm>

namespace robot_console {

ResourceRef parseResource(const QString& id) {
  static const QString kEdge = QStringLiteral("route_edge:");
  static const QString kNode = QStringLiteral("route_node:");
  ResourceRef r;
  if (id.startsWith(kEdge)) {
    const QString rest = id.mid(kEdge.size());
    const int sep = rest.indexOf(QStringLiteral("__"));
    if (sep <= 0 || sep + 2 >= rest.size()) return r;
    QString s1 = rest.left(sep);
    QString s2 = rest.mid(sep + 2);
    if (s2 < s1) std::swap(s1, s2);
    r.kind = ResourceRef::Kind::Edge;
    r.a = s1;
    r.b = s2;
  } else if (id.startsWith(kNode)) {
    const QString s = id.mid(kNode.size());
    if (s.isEmpty()) return r;
    r.kind = ResourceRef::Kind::Node;
    r.a = s;
  }
  return r;
}

QString routeResourceId(const QString& from, const QString& to) {
  return QStringLiteral("route_edge:%1__%2").arg(std::min(from, to), std::max(from, to));
}

QString edgeKey(const QString& s1, const QString& s2) {
  return s1 < s2 ? s1 + QLatin1Char('|') + s2 : s2 + QLatin1Char('|') + s1;
}

bool isBlockOwner(const QString& owner) { return owner.startsWith(QStringLiteral("traffic_block:")); }

}  // namespace robot_console
