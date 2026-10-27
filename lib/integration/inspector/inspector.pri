# Optional fixed-storage read-only runtime inspector. MIT.
isEmpty(TELEMETRY_INSPECTOR_PRI_INCLUDED) {
    TELEMETRY_INSPECTOR_PRI_INCLUDED = 1
    include($$PWD/../dynamic/dynamic.pri)
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    HEADERS += $$files($$PWD/*.hpp)
}
