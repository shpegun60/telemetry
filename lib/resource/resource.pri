# Generic C++20 resources and complete-packet dispatch.
# Keep structural providers opt-in so ordinary file consumers do not acquire
# telemetry/reflection dependencies. MIT; this directory LICENSE applies.
isEmpty(RESOURCE_PRI_INCLUDED) {
    RESOURCE_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/..
    # The core has no compiled sources and does not include telemetry.
    HEADERS += \
        $$PWD/Resource.hpp \
        $$PWD/Types.hpp \
        $$PWD/File.hpp \
        $$PWD/FileView.hpp \
        $$PWD/FileSystem.hpp \
        $$PWD/BytesFile.hpp \
        $$PWD/ChunkWriter.hpp

    # The packet layer is compiled once by this manifest. Transport framing
    # and the application's telemetry control protocol remain separate.
    HEADERS += $$PWD/protocol/Protocol.hpp
    SOURCES += $$PWD/protocol/Protocol.cpp
    contains(CONFIG, resource_telemetry) {
        HEADERS += $$files($$PWD/telemetry/v3/*.hpp, true)
        SOURCES += $$PWD/telemetry/v3/detail/Values.cpp
        DISTFILES += $$PWD/telemetry/v3/README.md
    }
    DISTFILES += $$PWD/README.md $$PWD/LICENSE $$PWD/protocol/README.md
}
