# Included by the installed consumer's test project in offline mode. MIT.
find_package(Telemetry REQUIRED COMPONENTS core encoded_server read_many_client encoded_backend)
add_library(read-many-client-probe OBJECT ArmClient.cpp)
target_link_libraries(read-many-client-probe PRIVATE Telemetry::read_many_client Telemetry::encoded_backend)
foreach(mode IN ITEMS legacy batch)
    add_executable(read-many-${mode} ArmProbe.cpp)
    if(mode STREQUAL "legacy")
        target_compile_definitions(read-many-${mode} PRIVATE READ_MANY_LEGACY_PROBE=1)
    endif()
    target_link_libraries(read-many-${mode} PRIVATE Telemetry::core Telemetry::encoded_server)
    target_link_options(read-many-${mode} PRIVATE -nostartfiles --specs=nosys.specs
        -Wl,--gc-sections -Wl,-e,read_many_probe
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/read-many-${mode}.map)
endforeach()
