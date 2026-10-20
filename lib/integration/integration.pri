# Explicit integration selection; dependency modules remain independent. MIT.
contains(CONFIG, telemetry_encoded_backend) {
    include($$PWD/../client/client.pri)
    include($$PWD/../wire/wire.pri)
    HEADERS *= $$PWD/encoded/Backend.hpp $$PWD/encoded/RetryingBackend.hpp
}
contains(CONFIG, telemetry_rpc_backend) {
    CONFIG += telemetry_client_rpc
    include($$PWD/../client/client.pri)
    HEADERS *= $$PWD/rpc/Backend.hpp
}
contains(CONFIG, resource_telemetry):isEmpty(TELEMETRY_RESOURCE_ADAPTER_PRI_INCLUDED) {
    TELEMETRY_RESOURCE_ADAPTER_PRI_INCLUDED = 1
    include($$PWD/../telemetry/telemetry.pri)
    include($$PWD/../resource/resource.pri)
    HEADERS += $$PWD/telemetry/v3/DescriptorFile.hpp \
        $$PWD/telemetry/v3/ValuesFile.hpp \
        $$PWD/telemetry/v3/ValuesFormat.hpp \
        $$files($$PWD/telemetry/v3/detail/*.hpp)
    SOURCES += $$PWD/telemetry/v3/detail/Values.cpp
}
contains(CONFIG, resource_telemetry_rpc)|contains(CONFIG, resource_telemetry_rpc_sessions) {
    isEmpty(TELEMETRY_RPC_SERVER_PRI_INCLUDED) {
        TELEMETRY_RPC_SERVER_PRI_INCLUDED = 1
        include($$PWD/../telemetry/telemetry.pri)
        include($$PWD/../resource/resource.pri)
        include($$PWD/../wire/wire.pri)
        HEADERS += $$PWD/telemetry/v3/RpcMailbox.hpp
        SOURCES += $$PWD/telemetry/v3/RpcMailbox.cpp
    }
}
contains(CONFIG, resource_telemetry_rpc_sessions) {
    HEADERS *= $$PWD/telemetry/v3/RpcSessionPool.hpp
}
contains(CONFIG, resource_telemetry_encoded):isEmpty(TELEMETRY_ENCODED_SERVER_PRI_INCLUDED) {
    TELEMETRY_ENCODED_SERVER_PRI_INCLUDED = 1
    include($$PWD/../telemetry/telemetry.pri)
    include($$PWD/../wire/wire.pri)
    HEADERS += $$PWD/encoded/Dispatcher.hpp
    SOURCES += $$PWD/encoded/Dispatcher.cpp
}
