#!/usr/bin/env bash
set -euo pipefail

# shellcheck source=robot_wait.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/robot_wait.sh"

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

cancel_me="${tmp_dir}/cancel_me.yaml"
long_running="${tmp_dir}/long_running.yaml"
urgent="${tmp_dir}/urgent.yaml"

cat >"${cancel_me}" <<'YAML'
mission_id: cancel_me
frame_id: map
loop: false
goals:
  - x: 0.0
    y: 0.0
    yaw: 0.0
YAML

cat >"${long_running}" <<'YAML'
mission_id: long_running
frame_id: map
loop: false
goals:
  - x: 0.0
    y: 0.0
    yaw: 0.0
  - x: 0.2
    y: 0.0
    yaw: 0.0
  - x: 0.4
    y: 0.0
    yaw: 0.0
  - x: 0.6
    y: 0.0
    yaw: 0.0
YAML

cat >"${urgent}" <<'YAML'
mission_id: urgent_preempt
frame_id: map
loop: false
goals:
  - x: 1.0
    y: 0.0
    yaw: 0.0
YAML

ros2 service type /v2/enqueue_mission_profile | rg "robot_interfaces_mission/srv/EnqueueMission"
ros2 service type /v2/cancel_queued_mission | rg "robot_interfaces_mission/srv/CancelQueuedMission"
ros2 service type /v2/preempt_mission_profile | rg "robot_interfaces_mission/srv/PreemptMission"
ros2 topic type /mission_runner/state | rg "robot_interfaces/msg/RobotState"

service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/enqueue_mission_profile robot_interfaces_mission/srv/EnqueueMission \
  "{mission_file: \"${cancel_me}\", priority: 1, start_if_idle: false}" | rg "success=True"
service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/cancel_queued_mission robot_interfaces_mission/srv/CancelQueuedMission \
  "{mission_id: cancel_me, cancel_active: false}" | rg "success=True"

service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/enqueue_mission_profile robot_interfaces_mission/srv/EnqueueMission \
  "{mission_file: \"${long_running}\", priority: 1, start_if_idle: true}" | rg "success=True"

seen_long=false
deadline=$((SECONDS + 12))
while true; do
  active_goal="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState \
      --field active_goal_id 2>/tmp/robot_queue_control_state.err || true
  )"
  if grep -q "long_running" <<<"${active_goal}"; then
    seen_long=true
    break
  fi
  if (( SECONDS >= deadline )); then
    echo "long_running did not start; active_goal_id=${active_goal}" >&2
    exit 1
  fi
  sleep 1
done

service_response "${ROS2_CALL_TIMEOUT:-30}" /v2/preempt_mission_profile robot_interfaces_mission/srv/PreemptMission \
  "{mission_file: \"${urgent}\", priority: 100}" | rg "success=True"

seen_urgent=false
# 抢占走"取消在跑任务再启动"时比直接启动多一轮取消往返，留 60 秒。
deadline=$((SECONDS + 60))
while true; do
  active_goal="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState \
      --field active_goal_id 2>/tmp/robot_queue_control_state.err || true
  )"
  state="$(
    timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState \
      --field state 2>/tmp/robot_queue_control_state.err || true
  )"

  if grep -q "urgent_preempt" <<<"${active_goal}"; then
    seen_urgent=true
  fi
  if ${seen_long} && ${seen_urgent} && grep -q "FINISHED" <<<"${state}"; then
    echo "queued mission cancel and active mission preempt finished"
    exit 0
  fi
  if grep -q "FAILED\\|ERROR" <<<"${state}"; then
    echo "queue control failed with state: ${state}, active_goal_id: ${active_goal}" >&2
    exit 1
  fi
  if (( SECONDS >= deadline )); then
    echo "queue control did not finish; seen_urgent=${seen_urgent}, state=${state}, active_goal_id=${active_goal}" >&2
    # 失败现场：完整状态（含 message，模拟运动时是剩余距离）和最近事件。
    timeout 15s ros2 topic echo --once /mission_runner/state robot_interfaces/msg/RobotState >&2 || true
    service_response 30s /v2/list_mission_events robot_interfaces_mission/srv/ListMissionEvents \
      "{limit: 10}" >&2 || true
    exit 1
  fi
  sleep 1
done
