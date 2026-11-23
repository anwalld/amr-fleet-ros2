cmake_minimum_required(VERSION 3.20)
project(robot_bringup)

find_package(ament_cmake REQUIRED)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(launch_testing_ament_cmake REQUIRED)
  # launch 测试各用独立的 ROS 域：colcon 并行跑多个包时，同名话题和 TF 会串（夜跑 #2、#3 的"漂移"）。
  # 不 find_package(ament_cmake_ros)：它会把 BUILD_SHARED_LIBS 打开，改变本包库的链接方式。
  ament_index_get_prefix_path(ament_prefixes)
  find_file(RUN_TEST_ISOLATED run_test_isolated.py
    PATHS ${ament_prefixes} PATH_SUFFIXES share/ament_cmake_ros/cmake
    NO_DEFAULT_PATH REQUIRED)
  add_launch_test(test/test_mock_bringup.launch.py TIMEOUT 30 RUNNER "${RUN_TEST_ISOLATED}")
endif()

ament_package()
