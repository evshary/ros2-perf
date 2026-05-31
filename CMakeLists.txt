cmake_minimum_required(VERSION 3.8)
project(perf)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)

# Create msgs
find_package(rosidl_default_generators REQUIRED)
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/U8Array.msg"
)
ament_export_dependencies(rosidl_default_runtime)

# include directories
include_directories(include)

# Build
add_executable(ping src/ping.cpp)
rosidl_get_typesupport_target(cpp_typesupport_target
    ${PROJECT_NAME} rosidl_typesupport_cpp)
target_link_libraries(ping PRIVATE
  "${cpp_typesupport_target}"
  rclcpp::rclcpp
)
add_executable(pong src/pong.cpp)
rosidl_get_typesupport_target(cpp_typesupport_target
    ${PROJECT_NAME} rosidl_typesupport_cpp)
target_link_libraries(pong PRIVATE
  "${cpp_typesupport_target}"
  rclcpp::rclcpp
)
add_executable(throughput_send src/throughput_send.cpp)
rosidl_get_typesupport_target(cpp_typesupport_target
    ${PROJECT_NAME} rosidl_typesupport_cpp)
target_link_libraries(throughput_send PRIVATE
  "${cpp_typesupport_target}"
  rclcpp::rclcpp
)
add_executable(throughput_recv src/throughput_recv.cpp)
rosidl_get_typesupport_target(cpp_typesupport_target
    ${PROJECT_NAME} rosidl_typesupport_cpp)
target_link_libraries(throughput_recv PRIVATE
  "${cpp_typesupport_target}"
  rclcpp::rclcpp
)

install(TARGETS
  ping
  pong
  throughput_send
  throughput_recv
  DESTINATION lib/${PROJECT_NAME})

install(PROGRAMS
  scripts/generate_payload_plot.py
  scripts/latency_runner.py
  scripts/throughput_runner.py
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # comment the line when a copyright and license is added to all source files
  set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # comment the line when this package is in a git repo and when
  # a copyright and license is added to all source files
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
