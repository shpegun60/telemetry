# A pure C++ consumer of an already generated SDK; invoke from an external dir.
# Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
TEMPLATE = app
CONFIG += console c++20
CONFIG -= qt app_bundle
TARGET = codegen_cpp
isEmpty(CODEGEN_SDK): error("Pass CODEGEN_SDK=/absolute/path/to/generated")
INCLUDEPATH += $$CODEGEN_SDK/cpp
SOURCES += $$PWD/main.cpp
