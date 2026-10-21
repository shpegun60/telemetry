# Native codec and reflection, without Model runtime. MIT.
isEmpty(TELEMETRY_CODEC_PRI_INCLUDED) {
    TELEMETRY_CODEC_PRI_INCLUDED = 1
    include($$PWD/../../common/common.pri)
    CONFIG -= c++11 c++14 c++17
    CONFIG += c++20
    INCLUDEPATH += $$PWD/../../boost_pfr/include $$PWD/../../magic_enum
    HEADERS += $$files($$PWD/../../telemetry/codec/*.hpp) \
        $$PWD/../../telemetry/type/Traits.hpp \
        $$PWD/../../telemetry/type/WireTypes.hpp \
        $$files($$PWD/../../telemetry/reflection/*.hpp, true)
}
