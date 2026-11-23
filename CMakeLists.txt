cmake_minimum_required(VERSION 3.21)

project(iss_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network Svg)

add_executable(iss_tracker
  src/main.cpp
  src/iss_tracker_window.cpp
  src/world_map_widget.cpp
  resources.qrc
)

target_include_directories(iss_tracker PRIVATE src)
target_link_libraries(iss_tracker PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(iss_tracker PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(world_map_widget_test
  src/world_map_widget_test.cpp
  src/world_map_widget.cpp
)
target_include_directories(world_map_widget_test PRIVATE src)
target_link_libraries(world_map_widget_test PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(world_map_widget_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME world_map_widget_test COMMAND world_map_widget_test)
