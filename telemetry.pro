QT += widgets
TARGET = telemetry_playground
CONFIG += c++20 warn_on resource_telemetry

include(lib/telemetry/telemetry.pri)
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
FORMS += app/mainwindow.ui

# Show the current public library, protocol and test documentation in Creator.
DISTFILES += \
    .gitignore \
    .gitattributes \
    LICENSE \
    README.md \
    .github/workflows/ci.yml \
    lib/delegate/README.md \
    lib/delegate/LICENSE \
    tests/structured/README.md \
    $$files($$PWD/tests/structured/*, true) \
    web/package.json \
    web/telemetry.js

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
