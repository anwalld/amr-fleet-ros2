#!/usr/bin/env bash
# 交通用例的公共服务请求，运行器负责启动 core 栈。
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/robot_wait.sh"

svc() { service_response "${ROS2_CALL_TIMEOUT:-30}" "$@" | response_only; }

# request_path <robot> <goal_station> <站点...>：两站时为单段申请（rolling）；带 PATH=1 时按整条路径申请。
request_segment() {
  local robot="$1" from="$2" to="$3" goal="${4:-}"
  svc /v2/request_route_segment robot_interfaces_mission/srv/RequestRouteSegment \
    "{robot_id: '${robot}', from_station: '${from}', to_station: '${to}', priority: 10, station_path: [], goal_station: '${goal}'}"
}
request_whole_path() {
  local robot="$1"
  shift
  svc /v2/request_route_segment robot_interfaces_mission/srv/RequestRouteSegment \
    "{robot_id: '${robot}', from_station: '$1', to_station: '${!#}', priority: 10, station_path: $(yaml_flow_list "$@")}"
}
detect() { svc /v2/detect_traffic_deadlock robot_interfaces_mission/srv/DetectTrafficDeadlock "{}"; }
