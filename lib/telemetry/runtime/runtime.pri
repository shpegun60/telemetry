# Descriptor schema and dynamic wire values; no native Model dependencies. MIT.
isEmpty(TELEMETRY_RUNTIME_PRI_INCLUDED) {
    TELEMETRY_RUNTIME_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$clean_path($$PWD/../..)
    HEADERS += $$files($$PWD/*.hpp, true) \
        $$PWD/../descriptor/Format.hpp \
        $$PWD/../type/WireTypes.hpp
}
