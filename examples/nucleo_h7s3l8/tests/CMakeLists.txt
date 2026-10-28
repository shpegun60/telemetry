# Host qualification of the actual board protocol and dequeue gate. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryNucleoH7SHost LANGUAGES CXX)
get_filename_component(TELEMETRY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
file(TO_CMAKE_PATH "${COBS_ROOT}" COBS_ROOT)
if(NOT EXISTS "${COBS_ROOT}/src/cobs/Cobs.h")
  message(FATAL_ERROR "Set external COBS_ROOT")
endif()
add_executable(nucleo_h7s_host Host.cpp
  "${COBS_ROOT}/src/cobs/Decoder.cpp" "${COBS_ROOT}/src/cobs/Encoder.cpp"
  "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
  "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp"
  "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp"
  "${TELEMETRY_ROOT}/lib/integration/encoded/Dispatcher.cpp")
target_compile_features(nucleo_h7s_host PRIVATE cxx_std_20)
target_compile_options(nucleo_h7s_host PRIVATE -Wall -Wextra -Werror -pedantic-errors)
target_include_directories(nucleo_h7s_host PRIVATE "${CMAKE_CURRENT_LIST_DIR}/.."
  "${TELEMETRY_ROOT}/lib" "${TELEMETRY_ROOT}/lib/boost_pfr/include"
  "${TELEMETRY_ROOT}/lib/magic_enum" "${TELEMETRY_ROOT}/lib/delegate" "${COBS_ROOT}/src")
