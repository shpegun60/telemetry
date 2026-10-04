# Compile the transport-owned Bind/Exchange example only when selected.
# Keep peer/session routing outside both library manifests.
# MIT; repository LICENSE applies.
# Include lib/telemetry/telemetry.pri independently first.
# Neither telemetry.pri nor resource.pri includes this example.
isEmpty(STRUCTURED_PROTOCOL_EXAMPLE_PRI_INCLUDED) {
    STRUCTURED_PROTOCOL_EXAMPLE_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/..
    HEADERS += $$files($$PWD/*.hpp, true)
    SOURCES += $$PWD/Bind.cpp $$PWD/Exchange.cpp
    DISTFILES += $$PWD/README.md
}
