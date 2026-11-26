#!/bin/bash
set -e
source /home/ubuntu/robot-dagu-workspaces/merged-control-current-full.YlN4Os/robot_ws/install/setup.bash
exec ros2 run robot_console robot_console "$@"
