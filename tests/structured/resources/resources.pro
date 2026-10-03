# Core-only and v3 selections; resource never includes telemetry backwards.
TEMPLATE = app
TARGET = structured_resources
CONFIG -= qt debug debug_and_release
CONFIG += console release c++20
CONFIG -= app_bundle
SOURCES += $$PWD/QmakeCheck.cpp $$PWD/DeviceResources.cpp
HEADERS += $$PWD/DeviceResources.hpp
equals(MODE, v3) {
    CONFIG += resource_telemetry
    DEFINES += WITH_V3
    include($$PWD/../../../lib/telemetry/telemetry.pri)
    include($$PWD/../../../lib/telemetry/telemetry.pri)
}
include($$PWD/../../../lib/resource/resource.pri)
include($$PWD/../../../lib/resource/resource.pri)
