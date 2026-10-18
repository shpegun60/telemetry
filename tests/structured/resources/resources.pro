# Independent core, descriptor/Values, RPC client/server/session selections. MIT.
TEMPLATE = app
TARGET = structured_resources
CONFIG -= qt debug debug_and_release
CONFIG += console release c++20
CONFIG -= app_bundle
SOURCES += $$PWD/QmakeCheck.cpp $$PWD/DeviceResources.cpp
HEADERS += $$PWD/DeviceResources.hpp
equals(MODE, v3)|equals(MODE, both)|equals(MODE, all) {
    CONFIG += resource_telemetry
    DEFINES += WITH_V3
}
equals(MODE, rpc)|equals(MODE, both)|equals(MODE, client_server)|equals(MODE, sessions)|equals(MODE, all) {
    CONFIG += resource_telemetry_rpc
    DEFINES += WITH_RPC
}
equals(MODE, client)|equals(MODE, client_server)|equals(MODE, all) {
    CONFIG += resource_telemetry_rpc_client
    DEFINES += WITH_RPC_CLIENT
}
equals(MODE, sessions)|equals(MODE, all) {
    CONFIG += resource_telemetry_rpc_sessions
    DEFINES += WITH_RPC_SESSIONS
}
!equals(MODE, core):!equals(MODE, v3):!equals(MODE, rpc):!equals(MODE, both):!equals(MODE, client):!equals(MODE, client_server):!equals(MODE, sessions):!equals(MODE, all) {
    error("Select MODE=core, v3, rpc, both, client, client_server, sessions or all")
}
equals(MODE, client) {
    include($$PWD/../../../lib/telemetry/codec/codec.pri)
    include($$PWD/../../../lib/telemetry/codec/codec.pri)
}
!equals(MODE, core):!equals(MODE, client) {
    include($$PWD/../../../lib/telemetry/telemetry.pri)
    include($$PWD/../../../lib/telemetry/telemetry.pri)
}
include($$PWD/../../../lib/resource/resource.pri)
include($$PWD/../../../lib/resource/resource.pri)

# This is the actual qmake SOURCES list after repeated manifest inclusion.
# The runner also inspects generated OBJECTS, compile recipes and built files.
!write_file($$OUT_PWD/selected-sources.txt, SOURCES) {
    error(Cannot write selected source manifest)
}
!write_file($$OUT_PWD/selected-headers.txt, HEADERS) {
    error(Cannot write selected header manifest)
}
