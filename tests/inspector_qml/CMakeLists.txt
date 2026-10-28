# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(telemetry_inspector_qml_checks LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.4)
get_filename_component(TELEMETRY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BUILD_TESTING OFF CACHE BOOL "Build only the maintained Inspector checks" FORCE)
add_subdirectory("${TELEMETRY_ROOT}/examples/inspector_qml" example)
set_target_properties(telemetry-inspector-qml PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/example")
qt_add_executable(telemetry_inspector_qml_check main.cpp)
target_compile_features(telemetry_inspector_qml_check PRIVATE cxx_std_20)
target_link_libraries(telemetry_inspector_qml_check PRIVATE inspector_qml_support Qt6::Gui Qt6::Test)
target_include_directories(telemetry_inspector_qml_check PRIVATE
    "${TELEMETRY_ROOT}/examples/inspector_qml")
