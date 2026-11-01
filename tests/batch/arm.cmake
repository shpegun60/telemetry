# Object-only client probe plus linked server and unchanged legacy roots. MIT.
add_library(batch-client-probe OBJECT ArmClient.cpp)
target_link_libraries(batch-client-probe PRIVATE Telemetry::batch_client Telemetry::encoded_backend)
foreach(mode IN ITEMS legacy batch)
    add_executable(batch-${mode} ArmProbe.cpp)
    target_link_libraries(batch-${mode} PRIVATE Telemetry::encoded_server)
    if(mode STREQUAL "legacy")
        target_compile_definitions(batch-${mode} PRIVATE BATCH_LEGACY_PROBE=1)
    else()
        target_link_libraries(batch-${mode} PRIVATE Telemetry::batch_server)
    endif()
    target_link_options(batch-${mode} PRIVATE -nostartfiles --specs=nosys.specs
        -Wl,--gc-sections -Wl,-e,batch_probe
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/batch-${mode}.map)
endforeach()
