# Build against an installed, relocatable Telemetry package. MIT.
cmake_minimum_required(VERSION 3.20)
project(TelemetryMixedBatch LANGUAGES CXX)
find_package(Telemetry REQUIRED COMPONENTS batch_client batch_server encoded_backend)
add_executable(batch-host Host.cpp)
target_link_libraries(batch-host PRIVATE Telemetry::batch_client Telemetry::batch_server
    Telemetry::encoded_backend)
