# Optional runtime-schema admission over the existing portable client FIFO. MIT.
isEmpty(TELEMETRY_DYNAMIC_CLIENT_PRI_INCLUDED) {
    TELEMETRY_DYNAMIC_CLIENT_PRI_INCLUDED = 1
    include($$PWD/../../telemetry/runtime/runtime.pri)
    include($$PWD/../../client/client.pri)
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    HEADERS += $$files($$PWD/*.hpp)
}
