# resource.pri - qmake integration for the resource library.
# Authors: Ruslan Kovtun (shpegun60), codexAi. MIT; see LICENSE.
#
# The core and packet protocol need only C++20. For telemetry providers, include
# lib/telemetry/telemetry.pri first and set CONFIG += resource_telemetry before
# including this file. Telemetry must have its normal JSON support enabled.
isEmpty(RESOURCE_PRI_INCLUDED) {
    RESOURCE_PRI_INCLUDED = 1

    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/..

    HEADERS += \
        $$PWD/Types.hpp \
        $$PWD/File.hpp \
        $$PWD/FileSystem.hpp \
        $$PWD/ChunkWriter.hpp \
        $$PWD/protocol/Protocol.hpp

    SOURCES += $$PWD/protocol/Protocol.cpp

    contains(CONFIG, resource_telemetry) {
        contains(CONFIG, telemetry_no_json) {
            error(resource_telemetry requires telemetry JSON support for schema fingerprints)
        }

        HEADERS += \
            $$PWD/telemetry/TelemetryFiles.hpp \
            $$PWD/telemetry/SchemaFile.hpp \
            $$PWD/telemetry/CommandsFile.hpp \
            $$PWD/telemetry/ValuesFile.hpp \
            $$PWD/telemetry/detail/FloatText.hpp \
            $$PWD/telemetry/detail/Stream.hpp \
            $$PWD/telemetry/detail/Json.hpp

        SOURCES += \
            $$PWD/telemetry/SchemaFile.cpp \
            $$PWD/telemetry/CommandsFile.cpp \
            $$PWD/telemetry/ValuesFile.cpp
    }

    DISTFILES += \
        $$PWD/README.md \
        $$PWD/LICENSE \
        $$PWD/protocol/README.md \
        $$PWD/telemetry/README.md
}
