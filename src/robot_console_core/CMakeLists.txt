cmake_minimum_required(VERSION 3.22)
project(robot_console_core LANGUAGES CXX)

# 纯 CMake 包：只依赖 Qt 6，不依赖 ROS。Mac 上直接 cmake -S 编译；虚拟机 / CI 上由 colcon 编译。
# 虚拟机和 CI 是 Qt 6.4.2，这里只要求 6.4 及以上；用到 6.4 没有的 API 由虚拟机编译和 CI 拦下。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)

set(CORE_HEADERS
  include/robot_console_core/types.hpp
  include/robot_console_core/json_codec.hpp
  include/robot_console_core/data_source.hpp
  include/robot_console_core/operation_source.hpp
  include/robot_console_core/replay_source.hpp
  include/robot_console_core/resource_id.hpp
  include/robot_console_core/fleet_table_model.hpp
  include/robot_console_core/task_queue_model.hpp
  include/robot_console_core/traffic_models.hpp
  include/robot_console_core/base_map.hpp
  include/robot_console_core/map_scene.hpp
  include/robot_console_core/map_view.hpp
  include/robot_console_core/traffic_panel.hpp
  include/robot_console_core/main_window.hpp
  include/robot_console_core/smoke_metrics.hpp
  include/robot_console_core/smoke_run.hpp
)

add_library(robot_console_core STATIC
  ${CORE_HEADERS}
  src/types.cpp
  src/json_codec.cpp
  src/data_source.cpp
  src/replay_source.cpp
  src/resource_id.cpp
  src/fleet_table_model.cpp
  src/task_queue_model.cpp
  src/traffic_models.cpp
  src/base_map.cpp
  src/map_scene.cpp
  src/map_view.cpp
  src/traffic_panel.cpp
  src/main_window.cpp
  src/operation_panel.cpp
  src/smoke_metrics.cpp
  src/smoke_run.cpp
)
target_include_directories(robot_console_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(robot_console_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_options(robot_console_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)

add_executable(robot_console_replay apps/replay_main.cpp)
target_link_libraries(robot_console_replay PRIVATE robot_console_core)

include(CTest)
if(BUILD_TESTING)
  set(CORE_TESTS
    test_json_codec
    test_replay_source
    test_main_window
    test_operations
    test_fleet_model
    test_task_model
    test_resource_id
    test_map_scene
    test_base_map
    test_traffic
    test_smoke_metrics
  )
  foreach(name IN LISTS CORE_TESTS)
    add_executable(${name} test/${name}.cpp)
    target_link_libraries(${name} PRIVATE robot_console_core Qt6::Test)
    target_compile_definitions(${name} PRIVATE
      ROBOT_CONSOLE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/data")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  endforeach()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS robot_console_core EXPORT robot_console_coreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION include)
install(DIRECTORY include/ DESTINATION include)
install(TARGETS robot_console_replay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT robot_console_coreTargets
  NAMESPACE robot_console_core::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robot_console_core)
configure_package_config_file(cmake/robot_console_coreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/robot_console_coreConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robot_console_core)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/robot_console_coreConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robot_console_core)
