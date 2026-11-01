# Local native/encoded Telemetry and descriptor core. MIT.
isEmpty(TELEMETRY_STRUCTURED_PRI_INCLUDED) {
    TELEMETRY_STRUCTURED_PRI_INCLUDED = 1
    include($$PWD/codec/codec.pri)
    HEADERS += $$files($$PWD/*.hpp, true) \
        $$files($$PWD/slot/*.h)
    SOURCES += $$PWD/abi/StructuredAbi.cpp \
        $$PWD/model/Adapter.cpp
    contains(CONFIG, telemetry_read_many_core)|contains(CONFIG, telemetry_read_many_server) {
        SOURCES += $$PWD/encoded/ReadMany.cpp
    }
}
