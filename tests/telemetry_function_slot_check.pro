TEMPLATE = app
TARGET = telemetry_function_slot_check
CONFIG += console c++17 warn_on
CONFIG -= qt app_bundle debug_and_release debug_and_release_target
DESTDIR = $$OUT_PWD
include(../lib/telemetry/telemetry.pri)
SOURCES += TelemetryFunctionSlotCheck.cpp
QMAKE_CXXFLAGS += -Wextra -Werror
