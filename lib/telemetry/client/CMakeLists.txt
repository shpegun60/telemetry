# Independent optional client targets. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.
cmake_minimum_required(VERSION 3.16)
add_library(telemetry_client INTERFACE)
target_compile_features(telemetry_client INTERFACE cxx_std_20)
target_include_directories(telemetry_client INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../..")
option(TELEMETRY_CLIENT_RPC "Build the native one-mailbox RPC client" OFF)
if(TELEMETRY_CLIENT_RPC)
    add_library(telemetry_rpc_client STATIC rpc/RpcClient.cpp)
    target_link_libraries(telemetry_rpc_client PUBLIC telemetry_client)
    target_include_directories(telemetry_rpc_client PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../../boost_pfr/include"
        "${CMAKE_CURRENT_LIST_DIR}/../../magic_enum")
endif()
