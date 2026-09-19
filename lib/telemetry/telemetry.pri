# Copy the telemetry and sibling delegate directories into a consumer.
# The library itself uses only C++17; Qt belongs to the application.
include($$PWD/../delegate/delegate.pri)
CONFIG += c++17
INCLUDEPATH += $$PWD
HEADERS += \
    $$PWD/TelemetryCompiler.h \
    $$PWD/TelemetryScalar.h \
    $$PWD/TelemetryConversion.h \
    $$PWD/TelemetryGetter.h \
    $$PWD/TelemetrySetter.h \
    $$PWD/TelemetryCatalog.h \
    $$PWD/TelemetryIndex.h \
    $$PWD/TelemetryJson.h
SOURCES += $$PWD/TelemetryJson.cpp
DISTFILES += $$PWD/LICENSE $$PWD/README.md
