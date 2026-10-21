# Host demonstration using the independently installed modules. MIT.
cmake_minimum_required(VERSION 3.20)
project(TelemetryDynamicHost LANGUAGES CXX)
find_package(Telemetry REQUIRED COMPONENTS dynamic_client encoded_backend encoded_server core)
add_executable(dynamic-host Host.cpp)
target_link_libraries(dynamic-host PRIVATE Telemetry::dynamic_client Telemetry::encoded_backend
    Telemetry::encoded_server Telemetry::core)
