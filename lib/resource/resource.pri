# Generic file providers and the complete-packet Resource protocol. MIT.
isEmpty(RESOURCE_PRI_INCLUDED) {
    RESOURCE_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/..
    HEADERS += $$files($$PWD/*.hpp, true)
    SOURCES += $$PWD/protocol/Protocol.cpp
}
