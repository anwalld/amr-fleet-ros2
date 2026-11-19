#!/usr/bin/env bash
# 任务中枢基本接口：服务与话题类型齐全；调用 /start_mission_profile 后默认任务跑完（FINISHED）。
# 原脚本假定启动时任务会自动开跑，运行器起栈时不自动开跑，所以这里自己发 start。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/robot_wait.sh"

for svc in /start_mission_profile /reload_mission_profile /pause_mission_profile /resume_mission_profile /cancel_mission_profile; do
  wait_service_type "${svc}" "std_srvs/srv/Trigger" 10 /tmp/robot_mission_runner_service.err
done
wait_service_type /v2/enqueue_mission_profile "robot_interfaces_mission/srv/EnqueueMission" 10 /tmp/robot_mission_runner_enqueue.err
wait_topic_type /mission_runner/state "robot_interfaces/msg/RobotState" 10 /tmp/robot_mission_runner_topic.err

out="$(timeout "${ROS2_CALL_TIMEOUT:-30}" ros2 service call /start_mission_profile std_srvs/srv/Trigger "{}")"
rg "success=True" <<<"${out}"
wait_active_mission demo_patrol 30

echo "mission runner passed"
