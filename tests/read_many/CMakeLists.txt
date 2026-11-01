# Qualify only independently installed public components. MIT.
cmake_minimum_required(VERSION 3.20)
project(TelemetryReadManyTests LANGUAGES CXX)
option(READ_MANY_ARM_PROBE "Build offline selected ARM roots" OFF)
if(READ_MANY_ARM_PROBE)
    include(arm.cmake)
    return()
endif()
find_package(Telemetry REQUIRED COMPONENTS read_many_client encoded_backend encoded_server core rpc_backend)
foreach(test IN ITEMS Wire Server Client NoHeap)
    add_executable(read-many-${test} ${test}.cpp)
    target_link_libraries(read-many-${test} PRIVATE Telemetry::read_many_client
        Telemetry::encoded_backend Telemetry::encoded_server Telemetry::core Telemetry::rpc_backend)
    if(test STREQUAL "NoHeap" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_definitions(read-many-${test} PRIVATE READ_MANY_WRAP_ALLOCATIONS=1)
        target_link_options(read-many-${test} PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc
            -Wl,--wrap=realloc -Wl,--wrap=free)
    endif()
endforeach()
