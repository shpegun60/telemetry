# Host qualification of the actual board protocol and dequeue gate. MIT.
cmake_minimum_required(VERSION 3.21)
project(TelemetryNucleoH7SHost LANGUAGES CXX)
get_filename_component(TELEMETRY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
file(TO_CMAKE_PATH "${COBS_ROOT}" COBS_ROOT)
if(NOT EXISTS "${COBS_ROOT}/src/cobs/Cobs.h")
  message(FATAL_ERROR "Set external COBS_ROOT")
endif()
set(BOARD_SOURCES
  "${COBS_ROOT}/src/cobs/Decoder.cpp" "${COBS_ROOT}/src/cobs/Encoder.cpp"
  "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
  "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp"
  "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp"
  "${TELEMETRY_ROOT}/lib/integration/encoded/Dispatcher.cpp")
add_executable(nucleo_h7s_host Host.cpp ${BOARD_SOURCES})
add_executable(nucleo_h7s_read_many ReadMany.cpp ${BOARD_SOURCES}
  "${TELEMETRY_ROOT}/lib/telemetry/encoded/ReadMany.cpp"
  "${TELEMETRY_ROOT}/lib/integration/encoded/ReadManyDispatcher.cpp")
target_compile_definitions(nucleo_h7s_read_many PRIVATE TELEMETRY_NUCLEO_READ_MANY=1)
foreach(target nucleo_h7s_host nucleo_h7s_read_many)
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic-errors)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/.."
    "${TELEMETRY_ROOT}/lib" "${TELEMETRY_ROOT}/lib/boost_pfr/include"
    "${TELEMETRY_ROOT}/lib/magic_enum" "${TELEMETRY_ROOT}/lib/delegate" "${COBS_ROOT}/src")
endforeach()
