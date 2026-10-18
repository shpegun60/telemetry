# Generate a client SDK from an actual native model, outside the source tree.
# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
cmake_minimum_required(VERSION 3.20)
project(telemetry_codegen_examples LANGUAGES CXX)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
get_filename_component(TELEMETRY_ROOT "../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SDK "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(DESCRIPTOR "${CMAKE_CURRENT_BINARY_DIR}/descriptor.bin")
add_library(codegen_native STATIC
    "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
    "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp")
target_include_directories(codegen_native PUBLIC "${TELEMETRY_ROOT}/lib"
    "${TELEMETRY_ROOT}/lib/boost_pfr/include" "${TELEMETRY_ROOT}/lib/magic_enum")
add_library(codegen_rpc_runtime STATIC
    "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp"
    "${TELEMETRY_ROOT}/lib/integration/telemetry/v3/RpcMailbox.cpp"
    "${TELEMETRY_ROOT}/lib/client/rpc/RpcClient.cpp")
target_link_libraries(codegen_rpc_runtime PUBLIC codegen_native)
add_executable(codegen_export Export.cpp)
target_link_libraries(codegen_export PRIVATE codegen_native)
file(GLOB_RECURSE CODEGEN_SOURCES CONFIGURE_DEPENDS "${TELEMETRY_ROOT}/tools/telemetry_codegen/*.py")
add_custom_command(OUTPUT "${SDK}/manifest.json"
    COMMAND codegen_export "${DESCRIPTOR}"
    COMMAND "${Python3_EXECUTABLE}" -m tools.telemetry_codegen generate "${DESCRIPTOR}" --targets cpp --output "${SDK}"
    DEPENDS codegen_export ${CODEGEN_SOURCES}
    WORKING_DIRECTORY "${TELEMETRY_ROOT}" VERBATIM)
add_custom_target(codegen_sdk DEPENDS "${SDK}/manifest.json")
foreach(example IN ITEMS cpp encoded rpc)
    add_executable(codegen_${example} "${TELEMETRY_ROOT}/examples/codegen_${example}/main.cpp")
    add_dependencies(codegen_${example} codegen_sdk)
    target_include_directories(codegen_${example} PRIVATE "${SDK}/cpp")
    if(example STREQUAL "rpc")
        target_link_libraries(codegen_${example} PRIVATE codegen_rpc_runtime)
    elseif(example STREQUAL "encoded")
        target_link_libraries(codegen_${example} PRIVATE codegen_native)
    endif()
endforeach()
enable_testing()
foreach(example IN ITEMS cpp encoded rpc)
    add_test(NAME codegen_${example} COMMAND codegen_${example})
endforeach()
