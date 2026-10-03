# Explicitly selected upper protocol example. No reverse .pri includes. MIT.
TEMPLATE = app
TARGET = structured_protocol_example
CONFIG += console
CONFIG -= app_bundle
QT -= core gui
include($$PWD/../../../lib/telemetry/telemetry.pri)
include($$PWD/../../../examples/structured_protocol/protocol.pri)
# Repeated selection must not duplicate compiled sources.
include($$PWD/../../../examples/structured_protocol/protocol.pri)
SOURCES += $$PWD/QmakeCheck.cpp
HEADERS += $$PWD/Fixture.hpp
