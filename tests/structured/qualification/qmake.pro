# Multi-TU C++20 consumer without old JSON or the optional protocol example.
# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
QT -= gui
QT += core
CONFIG += console c++20 telemetry_no_json resource_structured
CONFIG -= app_bundle
TEMPLATE = app
TARGET = structured_qualification
include(../../../lib/telemetry_structured/structured.pri)
include(../../../lib/resource/resource.pri)
SOURCES += Check.cpp Provider.cpp Typed.cpp Encoded.cpp
