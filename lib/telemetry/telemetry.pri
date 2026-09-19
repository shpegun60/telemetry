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
    $$PWD/Telemetry.h \
    $$PWD/TelemetryCacheline.h \
    $$PWD/TelemetryCompiler.h \
    $$PWD/TelemetryScalar.h \
    $$PWD/TelemetryConversion.h \
    $$PWD/TelemetryId.h \
    $$PWD/TelemetryGetter.h \
    $$PWD/TelemetrySetter.h \
    $$PWD/TelemetryFieldType.h \
    $$PWD/TelemetryEnum.h \
    $$PWD/TelemetryField.h \
    $$PWD/TelemetryCatalog.h \
    $$PWD/TelemetryIndex.h \
    $$PWD/TelemetryAbi.h \
    $$PWD/TelemetryJson.h \
    $$PWD/core/TelemetryCompiler.h \
    $$PWD/core/TelemetryCacheline.h \
    $$PWD/core/TelemetryScalar.h \
    $$PWD/core/TelemetryConversion.h \
    $$PWD/field/TelemetryId.h \
    $$PWD/field/TelemetryGetter.h \
    $$PWD/field/TelemetrySetter.h \
    $$PWD/field/TelemetryFieldType.h \
    $$PWD/field/TelemetryEnum.h \
    $$PWD/field/TelemetryField.h \
    $$PWD/catalog/TelemetryCatalog.h \
    $$PWD/catalog/TelemetryIndex.h \
    $$PWD/abi/TelemetryAbi.h \
    $$PWD/serialization/TelemetryJson.h \
    $$PWD/detail/TelemetryNumberConversion.h \
    $$PWD/detail/TelemetryBounds.h \
    $$PWD/detail/TelemetryJsonWriter.h \
    $$PWD/detail/TelemetryJsonValue.h
SOURCES += $$PWD/abi/TelemetryAbi.cpp
!contains(CONFIG, telemetry_no_json) {
    SOURCES += $$PWD/serialization/TelemetryJson.cpp
}
DISTFILES += $$PWD/LICENSE $$PWD/README.md
