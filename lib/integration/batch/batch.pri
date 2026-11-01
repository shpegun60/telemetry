# Optional Mixed Batch API over the existing DynamicClient FIFO. MIT.
isEmpty(TELEMETRY_BATCH_CLIENT_PRI_INCLUDED) {
    TELEMETRY_BATCH_CLIENT_PRI_INCLUDED = 1
    include($$PWD/../dynamic/dynamic.pri)
    include($$PWD/../../wire/wire.pri)
    HEADERS *= $$PWD/Client.hpp
}
