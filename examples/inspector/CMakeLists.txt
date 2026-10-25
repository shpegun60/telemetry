# Standalone host consumer of the installed read-only Inspector module. MIT.
cmake_minimum_required(VERSION 3.20)
project(TelemetryInspectorHost LANGUAGES CXX)
find_package(Telemetry REQUIRED COMPONENTS inspector encoded_backend encoded_server core)
add_executable(inspector-host Host.cpp)
target_link_libraries(inspector-host PRIVATE Telemetry::inspector Telemetry::encoded_backend
    Telemetry::encoded_server Telemetry::core)
enable_testing()
add_test(NAME inspector-loopback COMMAND inspector-host)
add_test(NAME inspector-write-descriptor COMMAND inspector-host --write-demo-descriptor "${CMAKE_CURRENT_BINARY_DIR}/demo-descriptor.bin")
set_tests_properties(inspector-write-descriptor PROPERTIES FIXTURES_SETUP descriptor)
add_test(NAME inspector-file-metadata COMMAND inspector-host --descriptor "${CMAKE_CURRENT_BINARY_DIR}/demo-descriptor.bin")
set_tests_properties(inspector-file-metadata PROPERTIES FIXTURES_REQUIRED descriptor PASS_REGULAR_EXPRESSION "metadata-only")
