# Independent core, descriptor/Values, RPC and combined selections. MIT.
TEMPLATE = app
TARGET = structured_resources
CONFIG -= qt debug debug_and_release
CONFIG += console release c++20
CONFIG -= app_bundle
SOURCES += $$PWD/QmakeCheck.cpp $$PWD/DeviceResources.cpp
HEADERS += $$PWD/DeviceResources.hpp
equals(MODE, v3)|equals(MODE, both) {
    CONFIG += resource_telemetry
    DEFINES += WITH_V3
}
equals(MODE, rpc)|equals(MODE, both) {
    CONFIG += resource_telemetry_rpc
    DEFINES += WITH_RPC
}
!equals(MODE, core):!equals(MODE, v3):!equals(MODE, rpc):!equals(MODE, both) {
    error("Select MODE=core, MODE=v3, MODE=rpc or MODE=both")
}
!equals(MODE, core) {
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
