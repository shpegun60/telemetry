# Four explicit configurations; no adapter reaches backwards into another .pri.
TEMPLATE = app
TARGET = structured_resources
CONFIG -= qt debug debug_and_release
CONFIG += console release c++20 telemetry_no_json
CONFIG -= app_bundle
SOURCES += $$PWD/QmakeCheck.cpp $$PWD/DeviceResources.cpp
HEADERS += $$PWD/DeviceResources.hpp
equals(MODE, v2)|equals(MODE, both) {
    CONFIG += resource_telemetry
    DEFINES += WITH_V2
    include($$PWD/../../../lib/telemetry/telemetry.pri)
}
equals(MODE, v3)|equals(MODE, both) {
    CONFIG += resource_structured
    DEFINES += WITH_V3
    include($$PWD/../../../lib/telemetry_structured/structured.pri)
    include($$PWD/../../../lib/telemetry_structured/structured.pri)
}
include($$PWD/../../../lib/resource/resource.pri)
include($$PWD/../../../lib/resource/resource.pri)
