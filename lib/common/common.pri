# Shared fixed-storage callback utilities. MIT.
isEmpty(TELEMETRY_COMMON_PRI_INCLUDED) {
    TELEMETRY_COMMON_PRI_INCLUDED = 1
    INCLUDEPATH += $$PWD/..
    HEADERS += $$files($$PWD/*.hpp) $$PWD/../delegate/tiny_delegate.hpp
}
