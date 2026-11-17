"""M2 多机轻量仿真：不开 Gazebo。

拉起任务中枢（交通管制在进程内）、无 Nav2 的 navigate_sequence_server，以及 fleet_size 个
fleet_robot_agent_node（task_agent 模式、kinematic_sim:=true）。任务中枢的 robot_id 设成
dispatcher（不在车队里），所以车队任务全部派给代理执行。

  ros2 launch robot_bringup fleet_sim.launch.py fleet_size:=6 network:=workshop \
      traffic_reservation_mode:=rolling

M3：network 增加 workshop_2way（含双向段与短环，用于死锁压测与场景）；
traffic_deadlock_resolution_enabled（默认 true）透传给任务中枢，false 时只检测不解除。
start_stations:=a,b,c 覆盖各车起点（也是停车位），死锁场景脚本用它把车摆到指定站点。
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare

# 各路网的起点（也是每台车空闲时回去的停车位），按车号取前 fleet_size 个。
NETWORK_HOMES = {
    "workshop": ["park_1", "park_2", "park_3", "park_4", "park_5", "park_6"],
    "workshop_2way": ["park_1", "park_2", "park_3", "park_4", "park_5", "park_6"],
    "warehouse": ["dock", "packing", "storage_b", "storage_a", "receiving"],
}


def _agents(context):
    fleet_size = int(LaunchConfiguration("fleet_size").perform(context))
    network = LaunchConfiguration("network").perform(context)
    mode = LaunchConfiguration("traffic_reservation_mode").perform(context)
    max_linear = float(LaunchConfiguration("agent_max_linear_mps").perform(context))
    dwell = float(LaunchConfiguration("agent_dwell_s").perform(context))
    homes = NETWORK_HOMES.get(network, NETWORK_HOMES["workshop"])
    start_stations = LaunchConfiguration("start_stations").perform(context).strip()
    if start_stations:
        homes = [station.strip() for station in start_stations.split(",") if station.strip()]
    if fleet_size > len(homes):
        raise RuntimeError(f"network {network} only has {len(homes)} start stations")
    nodes = []
    for index in range(fleet_size):
        robot_id = f"robot_{index + 1}"
        nodes.append(
            Node(
                package="robot_simulation",
                executable="fleet_robot_agent_node",
                name=f"{robot_id}_agent",
                parameters=[
                    {
                        "mode": "task_agent",
                        "robot_id": robot_id,
                        "topic_namespace": f"/{robot_id}",
                        "initial_station_id": homes[index],
                        "home_station_id": homes[index],
                        "kinematic_sim": True,
                        "traffic_reservation_mode": mode,
                        "max_linear_mps": max_linear,
                        "dwell_s": dwell,
                    }
                ],
                output="screen",
            )
        )
    return nodes


def generate_launch_description():
    network = LaunchConfiguration("network")
    station_file = PathJoinSubstitution(
        [FindPackageShare("robot_tasks"), "config", "stations",
         [network, "_stations.yaml"]]
    )
    mission_runner_launch = PathJoinSubstitution(
        [FindPackageShare("robot_tasks"), "launch", "mission_runner.launch.py"]
    )
    return LaunchDescription(
        [
            DeclareLaunchArgument("fleet_size", default_value="6"),
            DeclareLaunchArgument("network", default_value="workshop",
                                  description="workshop、workshop_2way 或 warehouse"),
            DeclareLaunchArgument("traffic_reservation_mode", default_value="rolling"),
            DeclareLaunchArgument("traffic_deadlock_resolution_enabled", default_value="true"),
            DeclareLaunchArgument("start_stations", default_value="",
                                  description="逗号分隔的各车起点，空 = 路网默认停车位"),
            DeclareLaunchArgument("agent_max_linear_mps", default_value="0.8"),
            DeclareLaunchArgument("agent_dwell_s", default_value="3.0"),
            IncludeLaunchDescription(
                PythonLaunchDescriptionSource(mission_runner_launch),
                launch_arguments={
                    "robot_id": "dispatcher",
                    "station_file": station_file,
                    "fleet_size": LaunchConfiguration("fleet_size"),
                    "traffic_reservation_mode": LaunchConfiguration("traffic_reservation_mode"),
                    "traffic_deadlock_resolution_enabled":
                        LaunchConfiguration("traffic_deadlock_resolution_enabled"),
                    "autostart": "false",
                    "server_timeout_ms": "3000",
                    "return_to_dock_on_low_battery": "false",
                    "estimate_use_nav2_path": "false",
                }.items(),
            ),
            Node(
                package="robot_tasks",
                executable="navigate_sequence_server_node",
                parameters=[{"use_nav2_action": False, "simulate_without_nav2": True}],
                output="screen",
            ),
            OpaqueFunction(function=_agents),
        ]
    )
