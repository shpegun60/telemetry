# telemetry.pri - Reusable qmake integration for the standalone telemetry library.
# Authors: Ruslan Kovtun (shpegun60), codexAi.
# License: MIT; see LICENSE in this directory.
#
# Copy telemetry and its sibling delegate and magic_enum directories into a consumer.
# The library itself uses only C++17; Qt belongs to the application.
include($$PWD/../delegate/delegate.pri)
include($$PWD/../magic_enum/magic_enum.pri)
CONFIG += c++17
INCLUDEPATH += $$PWD
HEADERS += \
    $$PWD/TelemetryCompiler.h \
    $$PWD/TelemetryScalar.h \
    $$PWD/TelemetryFieldType.h \
    $$PWD/TelemetryEnum.h \
    $$PWD/TelemetryConversion.h \
    $$PWD/TelemetryGetter.h \
    $$PWD/TelemetrySetter.h \
    $$PWD/TelemetryCatalog.h \
    $$PWD/TelemetryIndex.h \
    $$PWD/TelemetryJson.h
SOURCES += $$PWD/TelemetryJson.cpp
DISTFILES += $$PWD/LICENSE $$PWD/README.md
