# Build the optional real-COBS application example from an explicit checkout.
# The telemetry core does not acquire a COBS, UART or storage dependency.
# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
cmake_minimum_required(VERSION 3.20)
project(telemetry_cobs_integration LANGUAGES CXX)
set(COBS_ROOT "" CACHE PATH "Checkout of https://github.com/shpegun60/cobs")
if(NOT EXISTS "${COBS_ROOT}/src/cobs/Cobs.h")
    message(FATAL_ERROR "Set COBS_ROOT to the COBS repository checkout")
endif()
set(TELEMETRY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(cobs_integration
    main.cpp
    "${TELEMETRY_ROOT}/examples/device_integration/Device.cpp"
    "${TELEMETRY_ROOT}/examples/device_integration/Api.cpp"
    "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
    "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp"
    "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp"
    "${COBS_ROOT}/src/cobs/Encoder.cpp"
    "${COBS_ROOT}/src/cobs/Decoder.cpp")
target_compile_features(cobs_integration PRIVATE cxx_std_20)
set_target_properties(cobs_integration PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(cobs_integration PRIVATE
    "${TELEMETRY_ROOT}/lib"
    "${TELEMETRY_ROOT}/lib/boost_pfr/include"
    "${TELEMETRY_ROOT}/lib/magic_enum"
    "${TELEMETRY_ROOT}/lib/delegate"
    "${COBS_ROOT}/src")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cobs_integration PRIVATE -Wall -Wextra -Werror -pedantic-errors)
endif()
enable_testing()
add_test(NAME cobs_integration COMMAND cobs_integration)
