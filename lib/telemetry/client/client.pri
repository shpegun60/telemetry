# Independent portable client module. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.
# Select telemetry_client_rpc for the existing one-mailbox RPC packet engine.
# The Resource server manifest never selects a client runtime.
isEmpty(TELEMETRY_CLIENT_PRI_INCLUDED) {
    TELEMETRY_CLIENT_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/../..
    HEADERS += $$files($$PWD/*.hpp) $$files($$PWD/encoded/*.hpp) \
        $$PWD/../protocol/EncodedWire.hpp
    contains(CONFIG, telemetry_client_rpc) {
        include($$PWD/../codec/codec.pri)
        HEADERS += $$files($$PWD/rpc/*.hpp)
        SOURCES += $$PWD/rpc/RpcClient.cpp
    }
    # Legacy include paths are available only to applications explicitly opting in.
    contains(CONFIG, telemetry_client_compat) {
        INCLUDEPATH += $$PWD/compat
    }
}
