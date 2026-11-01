# Installed-package consumer; no repository lib include directory. MIT.
cmake_minimum_required(VERSION 3.20)
project(TelemetryReadMany LANGUAGES CXX)
find_package(Telemetry REQUIRED COMPONENTS read_many_client encoded_backend encoded_server core resource)
add_executable(read-many-host Host.cpp)
target_link_libraries(read-many-host PRIVATE Telemetry::read_many_client Telemetry::encoded_backend
    Telemetry::encoded_server Telemetry::core Telemetry::resource)
