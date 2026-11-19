#!/usr/bin/env bash
# 车队代理派单（acceptance-suite-rework specs：车队代理派单验收；M2 的 fleet_robot_agent_node）。
# fleet_sim 栈：2 台代理车、workshop_2way 路网，任务中枢自身不在车队里。
#   1. 经 /v2/submit_order（fleet_station）下 2 单，都被受理；
#   2. /v2/list_remote_tasks 列出这两单并派给代理车；两单最终状态为 FINISHED；
#   3. /fleet/agent_events 每单一条记录，含 duration_s、wait_route_s、distance_m（均 > 0 或 >= 0）；
#   4. 两车回到各自停车位（park_1、park_2）；
#   5. 交通状态里不再有路段占用（只剩各车所在路口）。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

wait_submit_order 30 /tmp/robot_agent_task_submit.err
wait_service_type /v2/list_remote_tasks "robot_interfaces_fleet/srv/ListRemoteTasks" 30 /tmp/robot_agent_task_list.err
wait_service_type /v2/list_fleet_robots "robot_interfaces_fleet/srv/ListFleetRobots" 30 /tmp/robot_agent_task_robots.err

EVENTS="$(mktemp /tmp/robot_agent_events.XXXXXX)"
ECHO_PID=""
cleanup() {
  [[ -n "${ECHO_PID}" ]] && kill "${ECHO_PID}" >/dev/null 2>&1
  rm -f "${EVENTS}"
}
trap cleanup EXIT
timeout 300 ros2 topic echo /fleet/agent_events std_msgs/msg/String --field data > "${EVENTS}" 2>/dev/null &
ECHO_PID=$!
sleep 3

submit() {
  call_submit_order "$1" fleet_station 10 \
    "task_id=$1;required_capability=station_transport;pickup_station=$2;dropoff_station=$3;start_if_idle=true;preempt_current=false" \
    fleet_agent_check
}

# 1. 下单
for order in "agent_check_a receiving storage" "agent_check_b assembly packing"; do
  read -r task pickup dropoff <<<"${order}"
  out="$(submit "${task}" "${pickup}" "${dropoff}")"
  echo "${out}"
  expect_eq "${task} accepted" "$(resp_scalar accepted <<<"${out}")" True
done

# 2. 远程任务列表：派给代理车，最终 FINISHED
remote_tasks() {
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_remote_tasks \
    robot_interfaces_fleet/srv/ListRemoteTasks "{robot_id: '', state: ''}"
}
out="$(remote_tasks)"
for task in agent_check_a agent_check_b; do
  robot="$(resp_row task_ids "${task}" assigned_robot_ids <<<"${out}")"
  [[ "${robot}" == robot_1 || "${robot}" == robot_2 ]] || { echo "${task} assigned to '${robot}'" >&2; exit 1; }
  echo "ok: ${task} assigned to ${robot}"
done
deadline=$((SECONDS + 240))
while true; do
  out="$(remote_tasks || true)"
  a="$(resp_row task_ids agent_check_a states <<<"${out}" || true)"
  b="$(resp_row task_ids agent_check_b states <<<"${out}" || true)"
  [[ "${a}" == FINISHED && "${b}" == FINISHED ]] && break
  if (( SECONDS >= deadline )); then
    echo "remote tasks did not finish: agent_check_a=${a} agent_check_b=${b}" >&2
    exit 1
  fi
  sleep 2
done
echo "ok: both remote tasks FINISHED"

# 3. 每单一条代理事件
python3 - "${EVENTS}" <<'PY'
import json, sys
events = {}
for line in open(sys.argv[1]):
    line = line.strip()
    if line.startswith("{"):
        e = json.loads(line)
        events[e.get("task")] = e
for task in ("agent_check_a", "agent_check_b"):
    e = events.get(task)
    if e is None:
        sys.exit(f"no /fleet/agent_events record for {task}; got {sorted(k for k in events if k)}")
    if not (e["duration_s"] > 0 and e["wait_route_s"] >= 0 and e["distance_m"] > 0):
        sys.exit(f"bad event fields for {task}: {e}")
    print(f"ok: {task} by {e['robot']} duration_s={e['duration_s']} wait_route_s={e['wait_route_s']} distance_m={e['distance_m']}")
PY

# 4. 回到停车位
deadline=$((SECONDS + 120))
while true; do
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_fleet_robots \
    robot_interfaces_fleet/srv/ListFleetRobots "{capability: station_transport, include_disabled: false}" || true)"
  s1="$(resp_row robot_ids robot_1 current_station_ids <<<"${out}" || true)"
  s2="$(resp_row robot_ids robot_2 current_station_ids <<<"${out}" || true)"
  [[ "${s1}" == park_1 && "${s2}" == park_2 ]] && break
  if (( SECONDS >= deadline )); then
    echo "robots did not return home: robot_1=${s1} robot_2=${s2}" >&2
    exit 1
  fi
  sleep 2
done
echo "ok: robot_1 at park_1, robot_2 at park_2"

# 5. 交通状态里没有残留的路段占用
resources="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /traffic/state \
  robot_interfaces_mission/msg/TrafficState --field resource_ids)"
echo "${resources}"
if rg -q "route_edge:" <<<"${resources}"; then
  echo "route edges still held after both robots returned home" >&2
  exit 1
fi

echo "fleet agent station task passed"
