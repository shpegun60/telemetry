# Qt Widgets playground

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This directory is the local, simulated Model playground built by the root
[telemetry.pro](../telemetry.pro). It owns its Widgets window, demonstration
catalog and Resource providers. It does not connect to a physical device.

Open `telemetry.pro` in Qt Creator with a Qt 6 C++20 kit, or use that kit's qmake
from an external build directory. Its executable is `telemetry_playground`.

For the runtime Universal Inspector, open
[`examples/inspector_qml/CMakeLists.txt`](../examples/inspector_qml/CMakeLists.txt)
instead. That separate Qt Quick application can browse unknown Descriptor types
and read Fields through TCP or the optional serial adapter. Its
[README](../examples/inspector_qml/README.md) contains build and connection commands.

The root CMake project builds the independent library modules and does not build
either GUI. See the [example index](../examples/README.md) to select a program.
