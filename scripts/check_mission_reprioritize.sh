#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

first="${tmp_dir}/reprio_first.yaml"
second="${tmp_dir}/reprio_second.yaml"

cat >"${first}" <<'YAML'
mission_id: reprio_first
frame_id: map
loop: false
goals:
  - x: 0.0
    y: 0.0
    yaw: 0.0
  - x: 0.1
    y: 0.0
    yaw: 0.0
YAML

cat >"${second}" <<'YAML'
mission_id: reprio_second
frame_id: map
loop: false
goals:
  - x: 0.2
    y: 0.0
    yaw: 0.0
  - x: 0.3
    y: 0.0
    yaw: 0.0
YAML

wait_service_type /v2/enqueue_mission_profile "robot_interfaces_mission/srv/EnqueueMission" 10 /tmp/robot_reprio_enqueue.err
wait_service_type /v2/reprioritize_queued_mission "robot_interfaces_mission/srv/ReprioritizeQueuedMission" 10 /tmp/robot_reprio_single.err
wait_service_type /v2/list_mission_events "robot_interfaces_mission/srv/ListMissionEvents" 10 /tmp/robot_reprio_events.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_reprio_state_topic.err

timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/enqueue_mission_profile robot_interfaces_mission/srv/EnqueueMission \
  "{mission_file: \"${first}\", priority: 5, start_if_idle: false}" | rg "success=True"
timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/enqueue_mission_profile robot_interfaces_mission/srv/EnqueueMission \
  "{mission_file: \"${second}\", priority: 1, start_if_idle: false}" | rg "success=True"

timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/reprioritize_queued_mission robot_interfaces_mission/srv/ReprioritizeQueuedMission \
  "{mission_id: reprio_second, priority: 20}" \
  | rg "success=True|reprio_second|20|queue_size=2"

timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /start_mission_profile std_srvs/srv/Trigger "{}" | rg "success=True"

# 判定改为查任务事件日志（/v2/list_mission_events），不再轮询 /mission_runner/state：
# 两条任务目标点只差 0.1 米、很快跑完，虚拟机慢时轮询状态会错过 reprio_second（M1 记录的竞态）。
# 通过条件：reprio_first 已 FINISHED，且 reprio_second 的 FINISHED 不晚于 reprio_first 的 STARTING。
event_stamp() {
  local mission_id="$1"
  local state="$2"
  timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
    "{limit: 200, state_filter: ${state}, mission_id_filter: ${mission_id}}" 2>/tmp/robot_reprio_events.err \
    | python3 -c '
import re, sys
text = sys.stdin.read()
m = re.search(r"stamp=\[(.*?)\]", text)
stamps = [float(x.strip().strip("\x27\x22")) for x in m.group(1).split(",") if x.strip()] if m else []
print(min(stamps) if stamps else "")
'
}

deadline=$((SECONDS + 60))
first_finished=""
while true; do
  first_finished="$(event_stamp reprio_first FINISHED)"
  if [[ -n "${first_finished}" ]]; then
    break
  fi
  if timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
      "{limit: 20, state_filter: FAILED, mission_id_filter: reprio_first}" 2>/dev/null \
      | rg "loaded [1-9]" >/dev/null; then
    echo "reprioritize queue: reprio_first FAILED" >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "reprioritize queue did not finish reprio_first within 60s" >&2
    exit 1
  fi
  sleep 1
done

second_finished="$(event_stamp reprio_second FINISHED)"
first_started="$(event_stamp reprio_first STARTING)"
echo "reprio_second FINISHED=${second_finished} reprio_first STARTING=${first_started} FINISHED=${first_finished}"
if [[ -z "${second_finished}" || -z "${first_started}" ]]; then
  echo "reprioritize queue events missing: second_finished=${second_finished} first_started=${first_started}" >&2
  exit 1
fi
if ! python3 -c "import sys; sys.exit(0 if float('${second_finished}') <= float('${first_started}') else 1)"; then
  echo "reprio_second (priority 20) did not run before reprio_first (priority 5)" >&2
  exit 1
fi

echo "mission reprioritize passed"
