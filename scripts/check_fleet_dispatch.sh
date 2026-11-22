#!/usr/bin/env bash
# 车队派单（acceptance-suite-rework design D2：fleet_preview + fleet_runtime + fleet_task_pool 合并）。
# 三个旧用例的断言全部保留并拆开加强；三者各自完整跑的同一种本地 fleet_station 任务只跑一遍。
#   1. 车队列表里有本车 robot_1，能力含 station_transport（原 fleet_task_pool）
#   2. 只有本车可用时预览派给 robot_1、local_robot=True；总距离 = 接车距离 + 任务距离，
#      任务距离 = /v2/estimate_station_transport 的距离（原 fleet_preview 写死的 5.1/3.9/1.2 是
#      warehouse 站点的数，在 amr_demo 上对不上，只因 "success=True|…" 而一直"通过"）
#   3. 把 robot_2 设为 available、在 receiving、25.2 V：更新响应与车队列表都反映这些字段（原 fleet_runtime）
#   4. 再预览派给 robot_2、local_robot=False、接车距离 0、总距离 = 任务距离（原 fleet_preview）
#   5. 下单派给远程车 robot_2（原 fleet_runtime）
#   6. 把 robot_2 设为 charging、在 dock、23.0 V（原 fleet_preview / fleet_runtime）
#   7. 再下单只能派给本车，任务跑到 FINISHED（三个旧用例共有的本地任务）
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_service_type /v2/list_fleet_robots "robot_interfaces_fleet/srv/ListFleetRobots" 10 /tmp/robot_fleet_dispatch_list.err
wait_service_type /v2/preview_fleet_station_task "robot_interfaces_fleet/srv/PreviewFleetStationTask" 10 /tmp/robot_fleet_dispatch_preview.err
wait_service_type /v2/update_fleet_robot_state "robot_interfaces_fleet/srv/UpdateFleetRobotState" 10 /tmp/robot_fleet_dispatch_update.err
wait_service_type /v2/estimate_station_transport "robot_interfaces_mission/srv/EstimateStationTransport" 10 /tmp/robot_fleet_dispatch_estimate.err
wait_submit_order 10 /tmp/robot_fleet_dispatch_submit.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_fleet_dispatch_state_topic.err

# 站点运输要等装卸确认，后台自动发（见 robot_wait.sh）。
start_station_auto_confirm

call() { service_response "${ROS2_CALL_TIMEOUT:-30}" "$@" | response_only; }
list_robots() {
  call /v2/list_fleet_robots robot_interfaces_fleet/srv/ListFleetRobots "{capability: station_transport, include_disabled: false}"
}
preview() {
  call /v2/preview_fleet_station_task robot_interfaces_fleet/srv/PreviewFleetStationTask \
    "{required_capability: station_transport, pickup_station: receiving, dropoff_station: storage_a}"
}
update_robot2() {
  call /v2/update_fleet_robot_state robot_interfaces_fleet/srv/UpdateFleetRobotState \
    "{robot_id: robot_2, enabled: true, state: $1, battery_voltage: $2, current_station_id: $3, update_enabled: false, update_state: true, update_battery_voltage: true, update_current_station_id: true}"
}
expect_update() {
  local out="$1" state="$2" voltage="$3" station="$4"
  expect_eq "update success" "$(resp_scalar success <<<"${out}")" True
  expect_eq "update robot_id" "$(resp_scalar robot_id <<<"${out}")" robot_2
  expect_eq "update state" "$(resp_scalar state <<<"${out}")" "${state}"
  expect_close "update battery_voltage" "$(resp_scalar battery_voltage <<<"${out}")" "${voltage}"
  expect_eq "update current_station_id" "$(resp_scalar current_station_id <<<"${out}")" "${station}"
}

estimate="$(call /v2/estimate_station_transport robot_interfaces_mission/srv/EstimateStationTransport \
  "{pickup_station: receiving, dropoff_station: storage_a, nominal_speed_mps: 0.5, battery_voltage: 24.0}")"
expect_eq "estimate success" "$(resp_scalar success <<<"${estimate}")" True
task_distance="$(resp_scalar distance_m <<<"${estimate}")"

# 1. 车队列表
out="$(list_robots)"
expect_eq "list success" "$(resp_scalar success <<<"${out}")" True
resp_row robot_ids robot_1 capabilities <<<"${out}" | rg "station_transport"

# 2. 只有本车可用时的预览
out="$(preview)"
expect_eq "preview#1 success" "$(resp_scalar success <<<"${out}")" True
expect_eq "preview#1 assigned_robot_id" "$(resp_scalar assigned_robot_id <<<"${out}")" robot_1
expect_eq "preview#1 local_robot" "$(resp_scalar local_robot <<<"${out}")" True
expect_close "preview#1 task_distance_m" "$(resp_scalar task_distance_m <<<"${out}")" "${task_distance}"
approach="$(resp_scalar approach_distance_m <<<"${out}")"
expect_close "preview#1 total = approach + task" "$(resp_scalar estimated_total_distance_m <<<"${out}")" \
  "$(awk -v a="${approach}" -v b="${task_distance}" 'BEGIN { print a + b }')"

# 3. robot_2 可用、在 receiving
out="$(update_robot2 available 25.2 receiving)"
expect_update "${out}" available 25.2 receiving
out="$(list_robots)"
expect_eq "list robot_2 state" "$(resp_row robot_ids robot_2 states <<<"${out}")" available
expect_close "list robot_2 battery" "$(resp_row robot_ids robot_2 battery_voltage <<<"${out}")" 25.2
expect_eq "list robot_2 station" "$(resp_row robot_ids robot_2 current_station_ids <<<"${out}")" receiving

# 4. 预览改派给已在取货站的 robot_2
out="$(preview)"
expect_eq "preview#2 assigned_robot_id" "$(resp_scalar assigned_robot_id <<<"${out}")" robot_2
expect_eq "preview#2 local_robot" "$(resp_scalar local_robot <<<"${out}")" False
expect_close "preview#2 approach_distance_m" "$(resp_scalar approach_distance_m <<<"${out}")" 0.0
expect_close "preview#2 task_distance_m" "$(resp_scalar task_distance_m <<<"${out}")" "${task_distance}"
expect_close "preview#2 total = task" "$(resp_scalar estimated_total_distance_m <<<"${out}")" "${task_distance}"

# 5. 下单派给远程车
out="$(call_submit_order runtime_remote fleet_station 45 \
  "task_id=runtime_remote;required_capability=station_transport;pickup_station=receiving;dropoff_station=storage_a;start_if_idle=true;preempt_current=false" \
  fleet_station_task)"
expect_eq "remote submit accepted" "$(resp_scalar accepted <<<"${out}")" True
rg "remote_fleet_task_runtime_remote" <<<"${out}"
rg "dispatched remote fleet task runtime_remote to robot_2" <<<"${out}"

# 6. robot_2 去充电
out="$(update_robot2 charging 23.0 dock)"
expect_update "${out}" charging 23.0 dock

# 7. 只剩本车，下单后跑完
out="$(call_submit_order dispatch_local fleet_station 45 \
  "task_id=dispatch_local;required_capability=station_transport;pickup_station=receiving;dropoff_station=storage_a;start_if_idle=true;preempt_current=false" \
  fleet_station_task)"
expect_eq "local submit accepted" "$(resp_scalar accepted <<<"${out}")" True
rg "station_order_fleet_dispatch_local" <<<"${out}"
wait_active_mission station_order_fleet_dispatch_local 120

echo "fleet dispatch passed"
