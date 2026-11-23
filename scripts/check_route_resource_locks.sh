#!/usr/bin/env bash
# 路权验收（M2 滚动申请语义）。
# 旧语义（whole_route）：同一路线的第二单入队时被拒 "route resource locked"。
# 新语义（rolling，默认）：入队只校验路径存在、不锁，第二单正常排进任务队列；
# 执行时任务中枢按车辆号（robot_1）逐段申请，到达后释放身后路段，只留当前路口。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_submit_order 10 /tmp/robot_route_lock_submit.err
wait_service_type /v2/cancel_queued_mission "robot_interfaces_mission/srv/CancelQueuedMission" 10 /tmp/robot_route_lock_cancel.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_route_lock_events.err
wait_service_type /v2/list_traffic_reservations "robot_interfaces_mission/srv/ListTrafficReservations" 10 /tmp/robot_route_lock_list.err

# 站点运输要等装卸确认，后台自动发（见 robot_wait.sh）。
start_station_auto_confirm

_rg_out="$(call_submit_order route_lock_a station_transport 10 \
  "pickup_station=receiving;dropoff_station=storage_a;start_if_idle=false;preempt_current=false" \
  station_transport_order | response_only)"
rg "accepted=True" <<<"${_rg_out}"
rg "station_order_route_lock_a" <<<"${_rg_out}"
rg "queue_size=1" <<<"${_rg_out}"

# 新语义：同一路线的第二单不再被拒，而是排进任务队列。
call_submit_order route_lock_b station_transport 10 \
  "pickup_station=receiving;dropoff_station=storage_a;start_if_idle=false;preempt_current=false" \
  station_transport_order >/tmp/robot_route_lock_b.out
rg "accepted=True" /tmp/robot_route_lock_b.out
rg "station_order_route_lock_b" /tmp/robot_route_lock_b.out
rg "queue_size=2" /tmp/robot_route_lock_b.out

# 入队不锁路。
service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_traffic_reservations robot_interfaces_mission/srv/ListTrafficReservations "{}" \
  >/tmp/robot_route_lock_list.out
if rg "station_order_route_lock" /tmp/robot_route_lock_list.out >/dev/null; then
  echo "rolling mode must not lock routes at submit time" >&2
  cat /tmp/robot_route_lock_list.out >&2
  exit 1
fi

_rg_out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/cancel_queued_mission robot_interfaces_mission/srv/CancelQueuedMission \
  "{mission_id: station_order_route_lock_a, cancel_active: false}" | response_only)"
rg "success=True" <<<"${_rg_out}"
rg "removed queued mission station_order_route_lock_a" <<<"${_rg_out}"
rg "queue_size=1" <<<"${_rg_out}"

service_response "${ROS2_CALL_TIMEOUT:-30}" /start_mission_profile std_srvs/srv/Trigger "{}" | rg "success=True"

deadline=$((SECONDS + 120))
while true; do
  if service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
    "{limit: 80, state_filter: FINISHED, mission_id_filter: station_order_route_lock_b}" \
    2>/tmp/robot_route_lock_events.err | rg "states=\\['FINISHED'\\]" >/dev/null; then
    break
  fi
  if (( SECONDS >= deadline )); then
    echo "route lock mission did not finish" >&2
    exit 1
  fi
  sleep 1
done

# 完成后 robot_1 只占着所在的卸货站路口，身后路段都已释放。
service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/list_traffic_reservations robot_interfaces_mission/srv/ListTrafficReservations "{}" \
  >/tmp/robot_route_lock_final.out
rg "route_node:storage_a" /tmp/robot_route_lock_final.out
rg "robot_1" /tmp/robot_route_lock_final.out
if rg "route_edge:" /tmp/robot_route_lock_final.out >/dev/null; then
  echo "robot_1 still holds route edges after finishing" >&2
  cat /tmp/robot_route_lock_final.out >&2
  exit 1
fi

echo "route resource locks passed"
