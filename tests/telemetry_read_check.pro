TEMPLATE = app
TARGET = telemetry_read_check
CONFIG += console c++17 warn_on
CONFIG -= qt app_bundle debug_and_release debug_and_release_target
DESTDIR = $$OUT_PWD

include(../lib/telemetry/telemetry.pri)
SOURCES += TelemetryReadCheck.cpp
DISTFILES += TelemetryReadCompileFail.cpp ConversionCodegen.cpp DeclaredTypeCodegen.cpp ScalarStorageCodegen.cpp ScalarVisitCodegen.cpp IndexCodegen.cpp
QMAKE_CXXFLAGS += -Wextra -Werror
