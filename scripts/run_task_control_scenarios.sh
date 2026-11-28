#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "${1:?evidence directory}"
output="$(cd "$1" && pwd)"
tasks="$(ros2 pkg prefix robot_tasks)"
simulation="$(ros2 pkg prefix robot_simulation)"
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
printf 'case\tstatus\n' >"$output/results.tsv"

run_case() (
  set -euo pipefail
  local name="$1"
  local -a children=()
  launch() {
    local label="$1"
    shift
    "$@" >"$output/$name-$label.log" 2>&1 &
    children+=("$!")
  }
  cleanup() {
    if (("${#children[@]}" > 0)); then
      kill -TERM "${children[@]}" 2>/dev/null || true
      wait "${children[@]}" 2>/dev/null || true
    fi
  }
  trap cleanup EXIT
  export ROS_DOMAIN_ID="${TASK_CONTROL_ROS_DOMAIN_ID:-$((RANDOM % 120 + 80))}"
  export TASK_CONTROL_TRACE="$output/$name-trace.jsonl"
  if [[ "$name" == d16-delayed-reply ]]; then
    local mission="$output/d16-mission.yaml"
    {
      printf 'mission_id: pause_delayed_reply\nframe_id: map\nloop: false\ngoals:\n'
      for x in {1..40}; do printf '  - x: %s.0\n    y: 0.0\n    yaw: 0.0\n' "$x"; done
    } >"$mission"
    launch nav "$tasks/lib/robot_tasks/navigate_sequence_server_node" --ros-args \
      -p use_nav2_action:=false -p use_waypoint_follower:=false
    launch runner "$tasks/lib/robot_tasks/mission_runner_node" --ros-args \
      -p autostart:=false -p "mission_file:=$mission" -r /v2/set_sequence_pause:=/test/delayed_pause_reply
    PYTHONOPTIMIZE=0 timeout --kill-after=5 55 python3 "$script_dir/check_task_control_delayed_reply.py"
  elif [[ "$name" == d16-motion-cutover ]]; then
    local mission="$output/d16-motion.yaml"
    local next="$output/d16-motion-next.yaml"
    printf 'mission_id: d16_motion_first\nframe_id: map\nloop: false\ngoals:\n  - x: 10.0\n    y: 0.0\n    yaw: 0.0\n' >"$mission"
    sed 's/d16_motion_first/d16_motion_next/' "$mission" >"$next"
    launch nav "$tasks/lib/robot_tasks/navigate_sequence_server_node" --ros-args \
      -p use_nav2_action:=false -p use_waypoint_follower:=false -p simulated_motion_enabled:=true
    launch runner "$tasks/lib/robot_tasks/mission_runner_node" --ros-args \
      -p autostart:=false -p preflight_enabled:=false -p return_to_dock_on_low_battery:=false \
      -p "mission_file:=$mission"
    PYTHONOPTIMIZE=0 timeout --kill-after=5 55 python3 "$script_dir/check_task_control_motion_cutover.py" "$next"
  elif [[ "$name" == d16-route-hazard ]]; then
    launch nav "$tasks/lib/robot_tasks/navigate_sequence_server_node" --ros-args \
      -p use_nav2_action:=false -p use_waypoint_follower:=false
    launch runner "$tasks/lib/robot_tasks/mission_runner_node" --ros-args \
      -p autostart:=false -p return_to_dock_on_low_battery:=false
    PYTHONOPTIMIZE=0 timeout --kill-after=5 55 python3 "$script_dir/check_task_control_route_hazard.py"
  else
    local mode=rolling
    [[ "$name" != *whole-route ]] || mode=whole_route
    launch runner "$tasks/lib/robot_tasks/mission_runner_node" --ros-args \
      -p robot_id:=dispatcher -p fleet_size:=1 -p autostart:=false \
      -p return_to_dock_on_low_battery:=false -p "traffic_reservation_mode:=$mode" \
      -p "station_file:=$tasks/share/robot_tasks/config/stations/workshop_2way_stations.yaml"
    launch alarm "$simulation/lib/robot_simulation/fleet_sim_stop_node"
    local agent_binary="${TASK_CONTROL_AGENT_PATH:-$simulation/lib/robot_simulation/fleet_robot_agent_node}"
    if [[ "$name" == fleet-restart-* ]]; then
      export FLEET_RESTART_AGENT="$agent_binary" FLEET_CHECKPOINT_PATH="$output/$name-checkpoint"
      export FLEET_RESTART_LOG="$output/$name-agent.log" FLEET_RESTART_MODE="$mode"
      if [[ "$name" == fleet-restart-paused-* ]]; then export FLEET_RESTART_PAUSED=1; fi
      if [[ "$name" == fleet-restart-missing-route ]]; then export FLEET_RESTART_MISSING_ROUTE=1; fi
    else
      launch agent "$agent_binary" --ros-args \
        -p mode:=task_agent -p robot_id:=robot_1 -p initial_station_id:=park_1 -p home_station_id:=park_1 \
        -p fleet_sim_stop_enabled:=true -p remote_task_control_enabled:=true \
        -p "traffic_reservation_mode:=$mode" -p max_linear_mps:=0.55 -p dwell_s:=0.1
    fi
    if [[ "$name" == fleet-cancel-paused ]]; then export CANCEL_WHILE_PAUSED=1; fi
    PYTHONOPTIMIZE=0 timeout --kill-after=5 95 python3 "$script_dir/check_task_control_remote_motion.py"
  fi
)

failed=0
for scenario in d16-delayed-reply d16-route-hazard d16-motion-cutover fleet-rolling fleet-whole-route fleet-cancel-paused \
    fleet-restart-held fleet-restart-missing-route fleet-restart-paused-rolling fleet-restart-paused-whole-route; do
  run_case "$scenario" >"$output/$scenario.log" 2>&1 &
  if wait "$!" && python3 "$script_dir/verify_task_control_trace.py" \
      "$scenario" "$output/$scenario-trace.jsonl" >"$output/$scenario-trace-summary.json"; then
    printf '%s\tPASS\n' "$scenario" >>"$output/results.tsv"
  else
    printf '%s\tFAIL\n' "$scenario" >>"$output/results.tsv"
    failed=1
  fi
done
exit "$failed"
