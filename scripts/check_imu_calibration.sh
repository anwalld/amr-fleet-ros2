#!/usr/bin/env bash
# 100D4 IMU 磁校准（acceptance-suite-rework design D8 补覆盖）。self：只起解析节点，它订阅原始字节话题，
# 不需要真实串口。开始校准后 /imu_100d4/calibration_state 为 magnetic_calibration_active，停止后回到 idle。
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_self_launch_common.sh"

start_bg imu ros2 run robot_sensors imu_100d4_parser_node
wait_service_type /imu_100d4/start_magnetic_calibration "std_srvs/srv/Trigger" 20 "${LOG_DIR}/wait_start.err"
wait_service_type /imu_100d4/stop_magnetic_calibration "std_srvs/srv/Trigger" 20 "${LOG_DIR}/wait_stop.err"

expect_state() {
  local want="$1" deadline=$((SECONDS + 10)) got=""
  while (( SECONDS < deadline )); do
    got="$(topic_once /imu_100d4/calibration_state std_msgs/msg/String --field data 2>/dev/null | head -1 || true)"
    [[ "${got}" == "${want}" ]] && { echo "ok: calibration_state = ${want}"; return 0; }
    sleep 0.5
  done
  echo "calibration_state: got '${got}', want '${want}'" >&2
  return 1
}

expect_state idle
for step in "start magnetic_calibration_active" "stop idle"; do
  read -r verb state <<<"${step}"
  out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call "/imu_100d4/${verb}_magnetic_calibration" std_srvs/srv/Trigger "{}" | response_only)"
  expect_eq "${verb} success" "$(resp_scalar success <<<"${out}")" True
  expect_state "${state}"
done

echo "imu calibration passed"
