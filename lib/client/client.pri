# Portable client core and optional native RPC packet engine. MIT.
isEmpty(TELEMETRY_CLIENT_PRI_INCLUDED) {
    TELEMETRY_CLIENT_PRI_INCLUDED = 1
    include($$PWD/../common/common.pri)
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    HEADERS += $$files($$PWD/*.hpp)
}
contains(CONFIG, telemetry_client_rpc):isEmpty(TELEMETRY_NATIVE_RPC_PRI_INCLUDED) {
    TELEMETRY_NATIVE_RPC_PRI_INCLUDED = 1
    include($$PWD/../telemetry/codec/codec.pri)
    include($$PWD/../wire/wire.pri)
    HEADERS += $$files($$PWD/rpc/*.hpp) \
        $$PWD/../resource/Types.hpp $$PWD/../resource/protocol/Wire.hpp \
        $$PWD/../resource/protocol/Packets.hpp
    SOURCES += $$PWD/rpc/RpcClient.cpp
}
