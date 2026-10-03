# Generic C++20 resources, with explicitly selected wire v3 providers. MIT.
isEmpty(RESOURCE_PRI_INCLUDED) {
    RESOURCE_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/..
    HEADERS += $$PWD/Types.hpp $$PWD/File.hpp $$PWD/FileSystem.hpp $$PWD/ChunkWriter.hpp $$PWD/protocol/Protocol.hpp
    SOURCES += $$PWD/protocol/Protocol.cpp
    contains(CONFIG, resource_telemetry) {
        HEADERS += $$files($$PWD/telemetry/v3/*.hpp, true)
        SOURCES += $$PWD/telemetry/v3/detail/Values.cpp
        DISTFILES += $$PWD/telemetry/v3/README.md
    }
    DISTFILES += $$PWD/README.md $$PWD/LICENSE $$PWD/protocol/README.md
}
