# One C++20 telemetry module. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.
isEmpty(TELEMETRY_STRUCTURED_PRI_INCLUDED) {
    TELEMETRY_STRUCTURED_PRI_INCLUDED = 1
    include($$PWD/../delegate/delegate.pri)
    include($$PWD/../magic_enum/magic_enum.pri)
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/.. $$PWD/../boost_pfr/include $$PWD/../magic_enum
    HEADERS += $$files($$PWD/*.hpp, true) $$files($$PWD/slot/*.h, true)
    SOURCES += $$PWD/abi/StructuredAbi.cpp $$PWD/model/Adapter.cpp
    DISTFILES += $$PWD/README.md $$PWD/LICENSE $$PWD/slot/README.md
}
