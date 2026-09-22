QT += widgets
TARGET = telemetry_playground
CONFIG += c++20 warn_on

include(lib/telemetry/telemetry.pri)
CONFIG += resource_telemetry
include(lib/resource/resource.pri)
INCLUDEPATH += $$PWD/app

SOURCES += \
    app/main.cpp \
    app/mainwindow.cpp \
    app/demo/DemoCatalog.cpp \
    app/resources/DeviceResources.cpp

HEADERS += \
    app/mainwindow.h \
    app/demo/DemoCatalog.h \
    app/resources/DeviceResources.hpp

FORMS += \
    app/mainwindow.ui

# Show repository documentation and standalone checks in Qt Creator too.
DISTFILES += \
    .gitignore \
    .gitattributes \
    LICENSE \
    README.md \
    lib/delegate/README.md \
    lib/delegate/LICENSE \
    .github/workflows/ci.yml \
    tests/run_checks.py \
    tests/run_arm_checks.py \
    tests/resources/run.py \
    tests/resources/stack_check.py \
    tests/resources/no_json.pro \
    tests/resources/DecoderCheck.mjs \
    $$files($$PWD/tests/resources/*.hpp) \
    web/package.json \
    web/telemetryBinary.js \
    tests/resources/README.md \
    tests/resources/.clang-format \
    $$files($$PWD/tests/resources/*.cpp) \
    tests/README.md \
    tests/factory-codegen.json \
    $$files($$PWD/tests/*.pro) \
    $$files($$PWD/tests/*.cpp) \
    $$files($$PWD/tests/abi/*) \
    $$files($$PWD/tests/audit/*) \
    $$files($$PWD/tests/position_tables/*) \
    $$files($$PWD/tests/position_tables/h7s/*) \
    $$files($$PWD/tests/command_dispatch/h7s/*) \
    $$files($$PWD/tests/field_layout/*) \
    $$files($$PWD/tests/field_layout/h7s/*) \
    $$files($$PWD/tests/json_stack/*) \
    $$files($$PWD/archive/id_ranges/*)

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
