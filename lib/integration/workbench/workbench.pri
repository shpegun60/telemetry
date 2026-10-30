# Optional fixed-storage FieldWrite workflow; transport is selected separately. MIT.
isEmpty(TELEMETRY_WORKBENCH_PRI_INCLUDED) {
    TELEMETRY_WORKBENCH_PRI_INCLUDED = 1
    include($$PWD/../inspector/inspector.pri)
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    HEADERS += $$files($$PWD/*.hpp)
}
