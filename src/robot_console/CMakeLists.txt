cmake_minimum_required(VERSION 3.22)
project(robot_console LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ament_cmake REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(robot_console_core REQUIRED)
find_package(robot_interfaces_business REQUIRED)
find_package(robot_interfaces_mission REQUIRED)
find_package(std_msgs REQUIRED)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

add_library(robot_console_bridge STATIC
  include/robot_console/request_tracker.hpp
  include/robot_console/ros_conversions.hpp
  include/robot_console/ros_bridge.hpp
  src/request_tracker.cpp
  src/ros_conversions.cpp
  src/ros_bridge.cpp
)
target_include_directories(robot_console_bridge PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(robot_console_bridge PUBLIC robot_console_core::robot_console_core Qt6::Core)
ament_target_dependencies(robot_console_bridge PUBLIC
  nav_msgs rclcpp robot_interfaces_business robot_interfaces_mission std_msgs)
target_compile_options(robot_console_bridge PRIVATE -Wall -Wextra)

add_executable(robot_console src/main.cpp)
target_link_libraries(robot_console PRIVATE robot_console_bridge Qt6::Widgets)

install(TARGETS robot_console RUNTIME DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_ros_conversions test/test_ros_conversions.cpp)
  target_link_libraries(test_ros_conversions robot_console_bridge)
  ament_add_gtest(test_request_tracker test/test_request_tracker.cpp)
  target_link_libraries(test_request_tracker robot_console_bridge)
endif()

ament_package()
