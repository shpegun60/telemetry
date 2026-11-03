# Optional typed Mixed Batch; no runtime-schema or UI dependency. MIT.
isEmpty(TELEMETRY_BATCH_STATIC_CLIENT_PRI_INCLUDED) {
    TELEMETRY_BATCH_STATIC_CLIENT_PRI_INCLUDED = 1
    include($$PWD/../../client/client.pri)
    include($$PWD/../../wire/wire.pri)
    HEADERS *= $$PWD/StaticClient.hpp
}
