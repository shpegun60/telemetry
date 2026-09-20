# telemetry.pri - Reusable qmake integration for the standalone telemetry library.
# Authors: Ruslan Kovtun (shpegun60), codexAi.
# License: MIT; see LICENSE in this directory.
#
# Copy telemetry and its sibling magic_enum directory into a consumer.
# The library itself uses only C++17; Qt belongs to the application.
include($$PWD/../magic_enum/magic_enum.pri)
CONFIG += c++17
INCLUDEPATH += $$PWD
HEADERS += \
    $$PWD/core/TelemetryOwnerSlot.h \
    $$PWD/core/TelemetryFunctionSlot.h \
    $$PWD/Telemetry.h \
    $$PWD/core/TelemetryCompiler.h \
    $$PWD/core/TelemetryCacheline.h \
    $$PWD/core/TelemetryId.h \
    $$PWD/core/TelemetryScalar.h \
    $$PWD/core/TelemetryConversion.h \
    $$PWD/field/TelemetryGetter.h \
    $$PWD/field/TelemetrySetter.h \
    $$PWD/field/TelemetryFieldType.h \
    $$PWD/field/TelemetryEnum.h \
    $$PWD/field/TelemetryField.h \
    $$PWD/detail/TelemetryFieldBinding.h \
    $$PWD/field/TelemetryFieldFactory.h \
    $$PWD/field/TelemetryFieldTable.h \
    $$PWD/catalog/TelemetryGroup.h \
    $$PWD/catalog/TelemetryFieldCatalogTable.h \
    $$PWD/command/TelemetryCommandCatalogTable.h \
    $$PWD/field/TelemetryLimits.h \
    $$PWD/command/TelemetryCommand.h \
    $$PWD/command/TelemetryCommandArgs.h \
    $$PWD/detail/TelemetryCommandBinding.h \
    $$PWD/command/TelemetryCommandFactory.h \
    $$PWD/command/TelemetryCommandCatalog.h \
    $$PWD/command/TelemetryCommandCatalogIndex.h \
    $$PWD/command/TelemetryCommandIndex.h \
    $$PWD/command/TelemetryCommandTable.h \
    $$PWD/catalog/TelemetryCatalog.h \
    $$PWD/catalog/TelemetryIndex.h \
    $$PWD/abi/TelemetryAbi.h \
    $$PWD/serialization/TelemetryJson.h \
    $$PWD/serialization/TelemetryCommandJson.h \
    $$PWD/detail/TelemetryCallable.h \
    $$PWD/detail/TelemetryNumberConversion.h \
    $$PWD/detail/TelemetryBounds.h \
    $$PWD/detail/TelemetryJsonWriter.h \
    $$PWD/detail/TelemetryJsonValue.h
SOURCES += $$PWD/abi/TelemetryAbi.cpp
# The ABI anchor is mandatory in core-only builds too. Set telemetry_no_json
# before including this file; it removes serializers, not numeric/table support.
!contains(CONFIG, telemetry_no_json) {
    SOURCES += $$PWD/serialization/TelemetryJson.cpp
    SOURCES += $$PWD/serialization/TelemetryCommandJson.cpp
}
DISTFILES += $$PWD/LICENSE $$PWD/README.md
