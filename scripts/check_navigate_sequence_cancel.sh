#!/usr/bin/env bash
# 取消与 FollowWaypoints 暂停竞态（acceptance-suite-rework design D13）。self：真节点 + 假 Nav2。
#   1. 等待受理时暂停：目标一被受理就被取消；
#   2. 恢复：重发；
#   3. 暂停时取消没生效：一直重发到生效；
#   4. /cancel_mission：Nav2 目标被取消，序列以 ABORTED + "sequence cancelled" 结束，节点不退出；
#   5. 节点还能接下一个目标并跑完；
#   6. 标准 action 取消仍以 CANCELED 结束。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"
set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 120 + 40))}"
WORK="$(mktemp -d /tmp/robot_nav_cancel.XXXXXX)"
EVENTS="${WORK}/events"
FEEDBACK="${WORK}/send_goal.log"
touch "${EVENTS}"
PIDS=()
cleanup() {
  local status=$? pid
  for pid in "${PIDS[@]:-}"; do
    kill -TERM "-${pid}" >/dev/null 2>&1 || true
  done
  wait >/dev/null 2>&1 || true
  if [[ ${status} -eq 0 ]]; then
    rm -rf "${WORK}"
  else
    cat "${EVENTS}" >&2 || true
    echo "preserving logs in ${WORK}" >&2
  fi
  return "${status}"
}
trap cleanup EXIT

wait_event() {
  local deadline=$((SECONDS + 20))
  until rg -q "^$1 x=1\.0$" "${EVENTS}"; do
    if (( SECONDS >= deadline )); then
      echo "fake Nav2 never saw '$1 x=1.0'" >&2
      exit 1
    fi
    sleep 0.2
  done
  echo "ok: $1"
}
expect_no_event() {
  if rg -q "^$1 " "${EVENTS}"; then
    echo "unexpected fake Nav2 event '$1'" >&2
    exit 1
  fi
}
trigger() {
  local out
  out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" "$1" std_srvs/srv/Trigger "{}")"
  expect_eq "$1 success" "$(resp_scalar success <<<"${out}")" True
}

setsid python3 "${SCRIPT_DIR}/fake_nav2_action.py" "${WORK}" follow_waypoints > "${WORK}/fake.log" 2>&1 &
PIDS+=("$!")
setsid ros2 run robot_tasks navigate_sequence_server_node --ros-args -p use_nav2_action:=false \
  > "${WORK}/server.log" 2>&1 &
PIDS+=("$!")
wait_action_type /follow_waypoints "nav2_msgs/action/FollowWaypoints" 20 "${WORK}/wait.err"
wait_action_type /navigate_sequence "robot_interfaces/action/NavigateSequence" 20 "${WORK}/wait.err"
for svc in /pause_mission /resume_mission /cancel_mission; do
  wait_service_type "${svc}" "std_srvs/srv/Trigger" 20 "${WORK}/wait.err"
done

send_goal() {
  PYTHONUNBUFFERED=1 setsid timeout 90 ros2 action send_goal /navigate_sequence \
    robot_interfaces/action/NavigateSequence \
    "{goals: [{header: {frame_id: map}, pose: {position: {x: 1.0}, orientation: {w: 1.0}}}]}" \
    > "$1" 2>&1 &
  GOAL_PID=$!
  PIDS+=("${GOAL_PID}")
}

# 1. 等待受理时暂停
touch "${WORK}/delay"
send_goal "${FEEDBACK}"
wait_event "received 1"
trigger /pause_mission
expect_no_event "accepted 1"
rm "${WORK}/delay"
wait_event "accepted 1"
wait_event "canceled 1"

# 2. 恢复后重发
trigger /resume_mission
wait_event "accepted 2"

# 3. 取消没生效就一直重发
touch "${WORK}/reject_cancel"
trigger /pause_mission
deadline=$((SECONDS + 20))
until (( $(rg -c "^cancel_rejected 2 " "${EVENTS}" || echo 0) >= 2 )); do
  if (( SECONDS >= deadline )); then
    echo "cancel was not repeated while paused" >&2
    exit 1
  fi
  sleep 0.2
done
rm "${WORK}/reject_cancel"
wait_event "canceled 2"
trigger /resume_mission
wait_event "accepted 3"

# 4. 经服务取消（退出码因结果状态而异，只看输出）
trigger /cancel_mission
wait_event "canceled 3"
wait "${GOAL_PID}" || true
rg -q "Goal finished with status: ABORTED" "${FEEDBACK}"
rg -q "message: sequence cancelled" "${FEEDBACK}"
if rg -q "terminate called" "${WORK}/server.log"; then
  echo "navigate sequence server crashed on cancel" >&2
  exit 1
fi
echo "ok: cancel service ended the sequence"

# 5. 节点还活着
send_goal "${WORK}/second.log"
wait_event "accepted 4"
touch "${WORK}/finish"
wait_event "succeeded 4"
wait "${GOAL_PID}"
rg -q "Goal finished with status: SUCCEEDED" "${WORK}/second.log"
echo "ok: server accepted the next goal"

# 6. 标准 action 取消：直接调取消服务（全零 ID = 取消全部），不依赖 CLI 对 SIGINT 的处理
rm "${WORK}/finish"
send_goal "${WORK}/third.log"
wait_event "accepted 5"
out="$(service_response "${ROS2_CALL_TIMEOUT:-30}" /navigate_sequence/_action/cancel_goal action_msgs/srv/CancelGoal "{}")"
echo "${out}"
wait_event "canceled 5"
wait "${GOAL_PID}" || true
rg -q "Goal finished with status: CANCELED" "${WORK}/third.log"
if rg -q "terminate called" "${WORK}/server.log"; then
  echo "navigate sequence server crashed on action cancel" >&2
  exit 1
fi

echo "navigate sequence cancel passed"
