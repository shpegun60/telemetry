# Desktop consumer; both libraries are selected explicitly.
# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
QT += core
QT -= gui
TEMPLATE = app
TARGET = structured_client_qt
CONFIG += console c++20 resource_structured telemetry_no_json
CONFIG -= app_bundle
SOURCES += $$PWD/QtSmoke.cpp
HEADERS += $$PWD/Device.hpp
include($$PWD/../../lib/telemetry_structured/structured.pri)
include($$PWD/../../lib/resource/resource.pri)
