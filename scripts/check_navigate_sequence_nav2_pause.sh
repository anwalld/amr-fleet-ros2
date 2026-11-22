#!/usr/bin/env bash
# NavigateToPose 回退模式下暂停要停车（acceptance-suite-rework design D12）。self：真节点 + 假 Nav2。
#   1. 行驶中暂停：Nav2 目标被取消，期间不重发，序列反馈只报 paused；
#   2. 恢复：重发同一个目标；
#   3. 等待受理时暂停：目标一被受理就被取消；
#   4. 再恢复：重发，跑完后序列成功，且被暂停取消的目标不计入失败。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"
set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-$((RANDOM % 120 + 40))}"
WORK="$(mktemp -d /tmp/robot_nav2_pause.XXXXXX)"
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

setsid python3 "${SCRIPT_DIR}/fake_navigate_to_pose.py" "${WORK}" > "${WORK}/fake.log" 2>&1 &
PIDS+=("$!")
setsid ros2 run robot_tasks navigate_sequence_server_node --ros-args -p use_waypoint_follower:=false \
  -p simulate_without_nav2:=false > "${WORK}/server.log" 2>&1 &
PIDS+=("$!")
wait_action_type /navigate_to_pose "nav2_msgs/action/NavigateToPose" 20 "${WORK}/wait.err"
wait_action_type /navigate_sequence "robot_interfaces/action/NavigateSequence" 20 "${WORK}/wait.err"
wait_service_type /pause_mission "std_srvs/srv/Trigger" 20 "${WORK}/wait.err"
wait_service_type /resume_mission "std_srvs/srv/Trigger" 20 "${WORK}/wait.err"

PYTHONUNBUFFERED=1 setsid timeout 150 ros2 action send_goal --feedback /navigate_sequence \
  robot_interfaces/action/NavigateSequence \
  "{goals: [{header: {frame_id: map}, pose: {position: {x: 1.0}, orientation: {w: 1.0}}}]}" \
  > "${FEEDBACK}" 2>&1 &
GOAL_PID=$!
PIDS+=("${GOAL_PID}")

# 1. 行驶中暂停
wait_event "accepted 1"
trigger /pause_mission
wait_event "canceled 1"
sleep 1
seen="$(wc -l < "${FEEDBACK}")"
sleep 3
expect_no_event "received 2"
during_pause="$(tail -n "+$((seen + 1))" "${FEEDBACK}")"
rg -q "state: paused" <<<"${during_pause}"
if rg -q "state: navigating" <<<"${during_pause}"; then
  echo "reported navigating while paused" >&2
  exit 1
fi
echo "ok: paused without resend"

# 2、3. 恢复后重发；受理之前再暂停
touch "${WORK}/delay"
trigger /resume_mission
wait_event "received 2"
trigger /pause_mission
expect_no_event "accepted 2"
rm "${WORK}/delay"
wait_event "accepted 2"
wait_event "canceled 2"

# 4. 恢复并跑完
trigger /resume_mission
wait_event "accepted 3"
touch "${WORK}/finish"
wait_event "succeeded 3"
if ! wait "${GOAL_PID}"; then
  echo "navigate sequence goal did not finish" >&2
  cat "${FEEDBACK}" >&2
  exit 1
fi
rg -q "Goal finished with status: SUCCEEDED" "${FEEDBACK}"
rg -q "succeeded_goals: 1" "${FEEDBACK}"
rg -q "failed_goals: 0" "${FEEDBACK}"

echo "navigate sequence nav2 pause passed"
