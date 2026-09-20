TEMPLATE = app
TARGET = telemetry_traversal_check
CONFIG += console c++17 warn_on
CONFIG -= qt app_bundle debug_and_release debug_and_release_target
DESTDIR = $$OUT_PWD
include(../lib/telemetry/telemetry.pri)
SOURCES += TelemetryTraversalCheck.cpp
QMAKE_CXXFLAGS += -Wextra -Werror
