QT += widgets
TARGET = telemetry_playground
CONFIG += c++17 warn_on

include(lib/telemetry/telemetry.pri)
INCLUDEPATH += $$PWD/app

SOURCES += \
    app/main.cpp \
    app/mainwindow.cpp \
    app/demo/DemoCatalog.cpp

HEADERS += \
    app/mainwindow.h \
    app/demo/DemoCatalog.h

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
    tests/README.md \
    $$files($$PWD/tests/*.pro) \
    $$files($$PWD/tests/*.cpp) \
    $$files($$PWD/archive/id_ranges/*)

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
