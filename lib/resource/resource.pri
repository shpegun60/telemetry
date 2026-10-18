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
    HEADERS += $$PWD/protocol/Protocol.hpp \
        $$PWD/protocol/Wire.hpp \
        $$PWD/protocol/Client.hpp
    SOURCES += $$PWD/protocol/Protocol.cpp
    contains(CONFIG, resource_telemetry) {
        HEADERS += $$PWD/telemetry/v3/BinaryFormat.hpp \
            $$PWD/telemetry/v3/Descriptor.hpp \
            $$PWD/telemetry/v3/DescriptorFile.hpp \
            $$PWD/telemetry/v3/ValuesFile.hpp \
            $$files($$PWD/telemetry/v3/detail/*.hpp, true)
        SOURCES += $$PWD/telemetry/v3/detail/Values.cpp
    }
    contains(CONFIG, resource_telemetry_rpc) {
        HEADERS += $$PWD/telemetry/v3/RpcMailbox.hpp \
            $$PWD/telemetry/v3/RpcWire.hpp
        SOURCES += $$PWD/telemetry/v3/RpcMailbox.cpp
    }
    contains(CONFIG, resource_telemetry_rpc_client) {
        HEADERS += $$PWD/telemetry/v3/RpcClient.hpp \
            $$PWD/telemetry/v3/RpcWire.hpp
        SOURCES += $$PWD/telemetry/v3/RpcClient.cpp
    }
    # Pool users explicitly select this header-only module and the existing
    # server module. Selecting the server alone never exposes a pool dependency.
    contains(CONFIG, resource_telemetry_rpc_sessions) {
        !contains(CONFIG, resource_telemetry_rpc) {
            error(resource_telemetry_rpc_sessions requires resource_telemetry_rpc)
        }
        HEADERS += $$PWD/telemetry/v3/RpcSessionPool.hpp
    }
    contains(CONFIG, resource_telemetry)|contains(CONFIG, resource_telemetry_rpc)|contains(CONFIG, resource_telemetry_rpc_client)|contains(CONFIG, resource_telemetry_rpc_sessions) {
        DISTFILES += $$PWD/telemetry/v3/README.md
    }
    DISTFILES += $$PWD/README.md $$PWD/LICENSE $$PWD/protocol/README.md
}
