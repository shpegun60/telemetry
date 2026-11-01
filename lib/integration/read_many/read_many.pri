# Optional Encoded ReadMany API over the existing DynamicClient FIFO. MIT.
isEmpty(TELEMETRY_READ_MANY_CLIENT_PRI_INCLUDED) {
    TELEMETRY_READ_MANY_CLIENT_PRI_INCLUDED = 1
    include($$PWD/../dynamic/dynamic.pri)
    include($$PWD/../../wire/wire.pri)
    HEADERS += $$PWD/Client.hpp
}
