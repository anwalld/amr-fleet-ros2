#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
output="${1:?evidence directory}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
tasks="$(ros2 pkg prefix robot_tasks)"
simulation="$(ros2 pkg prefix robot_simulation)"
ln -s "$tasks/lib/robot_tasks/mission_runner_node" "$output/mission_runner_node"
ln -s "$tasks/lib/robot_tasks/navigate_sequence_server_node" "$output/navigate_sequence_server_node"
ln -s "$simulation/lib/robot_simulation/fleet_robot_agent_node" "$output/fleet_robot_agent_node"
cp "$tasks/share/robot_tasks/config/missions/demo_patrol.yaml" "$output/mission.yaml"
cd "$output"
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
failed=0
printf 'case\tstatus\n' > results.tsv
for scenario in d16 fleet dispatcher-state d16-state; do
  export ROS_DOMAIN_ID="${TASK_CONTROL_ROS_DOMAIN_ID:-$((RANDOM % 130 + 70))}"
  if [[ "$scenario" == *-state ]]; then
    command=(python3 "$script_dir/check_mission_restart_reconciliation.py" "${scenario%-state}"
             "$output/mission_runner_node" "$output/navigate_sequence_server_node"
             "$tasks/share/robot_tasks/config/stations/workshop_2way_stations.yaml"
             "$output/$scenario")
  else
    command=(python3 "$script_dir/check_task_control_restart.py" "$scenario")
  fi
  if PYTHONOPTIMIZE=0 timeout --kill-after=5 75 "${command[@]}" >"result-$scenario.log" 2>&1; then
    printf '%s\tPASS\n' "$scenario" >> results.tsv
  else
    printf '%s\tFAIL\n' "$scenario" >> results.tsv
    failed=1
  fi
done
exit "$failed"
