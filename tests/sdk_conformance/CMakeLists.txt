# Real installed consumers; Model fixtures remain application source. MIT.
cmake_minimum_required(VERSION 3.20)
project(TelemetrySdkConformance LANGUAGES CXX)
set(CONFORMANCE_GENERATED "" CACHE PATH "External generated C++ SDK")
option(CONFORMANCE_PRODUCER_ONLY "Build only the shared native Descriptor producer" OFF)
if(CONFORMANCE_PRODUCER_ONLY)
    find_package(Telemetry REQUIRED COMPONENTS core)
    add_executable(conformance-descriptor ../batch_static/Descriptor.cpp)
    target_link_libraries(conformance-descriptor PRIVATE Telemetry::core)
    return()
endif()
find_package(Telemetry REQUIRED COMPONENTS core encoded_backend encoded_server batch_server
    read_many_static_client batch_static_client read_many_client batch_client)
if(NOT EXISTS "${CONFORMANCE_GENERATED}/conformance_api/ClientBindings.hpp")
    message(FATAL_ERROR "CONFORMANCE_GENERATED requires the actual generated C++ SDK")
endif()
add_executable(conformance-native NativeConformance.cpp)
target_include_directories(conformance-native PRIVATE "${CONFORMANCE_GENERATED}")
target_link_libraries(conformance-native PRIVATE Telemetry::core Telemetry::encoded_backend
    Telemetry::batch_server Telemetry::encoded_server Telemetry::read_many_static_client
    Telemetry::batch_static_client Telemetry::read_many_client Telemetry::batch_client)
add_executable(conformance-peer ../client_dynamic_web/Bridge.cpp)
target_link_libraries(conformance-peer PRIVATE Telemetry::core Telemetry::encoded_server Telemetry::batch_server)
