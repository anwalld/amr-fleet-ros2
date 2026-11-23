#!/usr/bin/env bash
# 路径跟踪与实验统计（acceptance-suite-rework design D8 补覆盖）。self：tracking.launch.py + mock 底盘。
# pure_pursuit 与 stanley 两种控制器各起一次：/reference_path 有路径点；/tracking_cmd_vel 发出非零速度；
# /tracking_error 在发布；/experiment/statistics 的 total_goals（采样数）大于 0。
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_self_launch_common.sh"

for controller in pure_pursuit stanley; do
  start_bg "tracking_${controller}" ros2 launch robot_bringup tracking.launch.py \
    controller:="${controller}" use_mock_chassis:=true \
    output_csv:="${LOG_DIR}/${controller}.csv" output_json:="${LOG_DIR}/${controller}.json"
  wait_topic_type /reference_path "nav_msgs/msg/Path" 30 "${LOG_DIR}/wait_path.err"

  poses="$(topic_once /reference_path nav_msgs/msg/Path | rg -c "position:" || true)"
  (( poses > 1 )) || { echo "${controller}: /reference_path has ${poses} poses" >&2; exit 1; }
  echo "ok: ${controller} /reference_path ${poses} poses"

  deadline=$((SECONDS + 20)); vx=0
  while (( SECONDS < deadline )); do
    vx="$(topic_once /tracking_cmd_vel geometry_msgs/msg/Twist --field linear.x 2>/dev/null | head -1 || echo 0)"
    awk -v v="${vx}" 'BEGIN { exit !(v > 0.001 || v < -0.001) }' && break
    sleep 0.5
  done
  awk -v v="${vx}" 'BEGIN { exit !(v > 0.001 || v < -0.001) }' || { echo "${controller}: /tracking_cmd_vel stayed zero" >&2; exit 1; }
  echo "ok: ${controller} /tracking_cmd_vel linear.x=${vx}"

  topic_once /tracking_error robot_interfaces/msg/TrackingError >/dev/null
  echo "ok: ${controller} /tracking_error publishing"

  deadline=$((SECONDS + 20)); total=0
  while (( SECONDS < deadline )); do
    total="$(topic_once /experiment/statistics robot_interfaces/msg/NavigationStatistics --field total_goals 2>/dev/null | head -1 || echo 0)"
    (( total > 0 )) && break
    sleep 1
  done
  (( total > 0 )) || { echo "${controller}: /experiment/statistics total_goals=${total}" >&2; exit 1; }
  echo "ok: ${controller} /experiment/statistics total_goals=${total}"

  stop_all_bg
done

echo "path tracking passed"
