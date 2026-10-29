# Independent C++20 modules and relocatable installation. MIT.
cmake_minimum_required(VERSION 3.20)
project(Telemetry LANGUAGES CXX)
# Give GNUInstallDirs and the generated install/export rules the same lexical
# paths. Filesystem symlinks must not reinterpret a later uncollapsed "..".
foreach(_directory CMAKE_INSTALL_PREFIX CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
        CMAKE_INSTALL_DATAROOTDIR CMAKE_INSTALL_DATADIR)
    if(DEFINED ${_directory})
        cmake_path(SET ${_directory} NORMALIZE "${${_directory}}")
    endif()
endforeach()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    message(FATAL_ERROR "Relocatable Telemetry modules require relative include and library install directories")
endif()

# Select only the modules an application needs. Dependencies are added below.
if(NOT DEFINED TELEMETRY_COMPONENTS)
    set(TELEMETRY_COMPONENTS "core;runtime;dynamic_client;inspector;resource_protocol;client;resource_adapter;encoded_server;rpc_sessions;encoded_backend;rpc_backend;tcp_server"
        CACHE STRING "Telemetry modules to build and install")
endif()
include(cmake/Modules.cmake)

install(EXPORT TelemetryTargets NAMESPACE Telemetry:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Telemetry)
configure_package_config_file(cmake/TelemetryConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/TelemetryConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Telemetry)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/TelemetryConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Telemetry)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/telemetry)
if(TARGET telemetry_delegate)
    install(FILES lib/delegate/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/telemetry/dependencies/delegate)
endif()
if(TARGET telemetry_reflection)
    install(FILES lib/boost_pfr/LICENSE_1_0.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/telemetry/dependencies/boost_pfr)
    install(FILES lib/magic_enum/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/telemetry/dependencies/magic_enum)
endif()
