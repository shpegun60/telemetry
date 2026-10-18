# Explicit header-only canonical codec and type dependencies. MIT.
# Client-only consumers include this before resource.pri; no Model adapter or
# compiled layout boundary is selected by this independent manifest.
isEmpty(TELEMETRY_CODEC_PRI_INCLUDED) {
    TELEMETRY_CODEC_PRI_INCLUDED = 1
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/../.. $$PWD/../../boost_pfr/include $$PWD/../../magic_enum
    HEADERS += $$files($$PWD/*.hpp) \
        $$files($$PWD/../type/*.hpp) \
        $$files($$PWD/../reflection/*.hpp) \
        $$PWD/../core/Compiler.hpp
    DISTFILES += $$PWD/../README.md $$PWD/../LICENSE
}
