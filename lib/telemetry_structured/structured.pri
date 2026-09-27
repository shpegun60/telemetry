# Explicit qmake integration for the C++20 structured module.
# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
# This file does not include telemetry.pri or resource.pri. Consumers select
# those independently; resource_structured only enables the resource adapter.
isEmpty(TELEMETRY_STRUCTURED_PRI_INCLUDED) {
    TELEMETRY_STRUCTURED_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/.. $$PWD/../boost_pfr/include $$PWD/../magic_enum
    HEADERS += $$files($$PWD/*.hpp, true)
    SOURCES += $$PWD/abi/StructuredAbi.cpp $$PWD/model/Adapter.cpp
}
