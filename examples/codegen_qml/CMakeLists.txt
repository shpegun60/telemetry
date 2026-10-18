# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)
project(telemetry_codegen_qml_example LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.4)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
get_filename_component(TELEMETRY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT GENERATED_SDK)
    message(FATAL_ERROR "Set GENERATED_SDK to the native fixture SDK root")
endif()
add_subdirectory("${GENERATED_SDK}/qml" generated)
qt_add_executable(telemetry_qml_example main.cpp)
qt_add_qml_module(telemetry_qml_example URI Telemetry.Example VERSION 1.0
    RESOURCE_PREFIX /qt/qml QML_FILES Main.qml)
target_compile_features(telemetry_qml_example PRIVATE cxx_std_20)
target_compile_definitions(telemetry_qml_example PRIVATE QT_NO_KEYWORDS QT_NO_FOREACH)
target_include_directories(telemetry_qml_example PRIVATE
    "${TELEMETRY_ROOT}/lib" "${TELEMETRY_ROOT}/lib/boost_pfr/include"
    "${TELEMETRY_ROOT}/lib/magic_enum")
target_sources(telemetry_qml_example PRIVATE
    "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
    "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp"
    "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp"
    "${TELEMETRY_ROOT}/lib/integration/telemetry/v3/RpcMailbox.cpp"
    "${TELEMETRY_ROOT}/lib/integration/encoded/Dispatcher.cpp"
    "${TELEMETRY_ROOT}/lib/client/rpc/RpcClient.cpp")
target_link_libraries(telemetry_qml_example PRIVATE telemetry_qml_device_api
    telemetry_qml_device_apiplugin Qt6::Quick Qt6::QuickControls2)
qt_import_qml_plugins(telemetry_qml_example)
