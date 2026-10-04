# This builds the application example; it does not introduce library packaging.
cmake_minimum_required(VERSION 3.20)
project(telemetry_device_integration LANGUAGES CXX)
set(TELEMETRY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(device_integration
    main.cpp Device.cpp Api.cpp
    "${TELEMETRY_ROOT}/lib/telemetry/abi/StructuredAbi.cpp"
    "${TELEMETRY_ROOT}/lib/telemetry/model/Adapter.cpp"
    "${TELEMETRY_ROOT}/lib/resource/protocol/Protocol.cpp")
target_compile_features(device_integration PRIVATE cxx_std_20)
set_target_properties(device_integration PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(device_integration PRIVATE
    "${TELEMETRY_ROOT}/lib"
    "${TELEMETRY_ROOT}/lib/boost_pfr/include"
    "${TELEMETRY_ROOT}/lib/magic_enum")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(device_integration PRIVATE -Wall -Wextra -Werror -pedantic-errors)
elseif(MSVC)
    target_compile_options(device_integration PRIVATE /W4 /WX)
endif()
enable_testing()
add_test(NAME device_integration COMMAND device_integration)
