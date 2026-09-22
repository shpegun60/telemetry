# Standalone resource adapter integration without telemetry JSON (MIT).
TEMPLATE = app
TARGET = resource_no_json
QT -= core gui
CONFIG -= app_bundle
CONFIG += console c++20 telemetry_no_json resource_telemetry
include(../../lib/telemetry/telemetry.pri)
include(../../lib/resource/resource.pri)
SOURCES += DeviceCheck.cpp ../../app/resources/DeviceResources.cpp ../../app/demo/DemoCatalog.cpp
