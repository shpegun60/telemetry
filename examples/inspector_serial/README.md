# Shared Inspector serial profile

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This directory owns only [SerialProfile.hpp](SerialProfile.hpp), the common
COBS/CRC16 framing selection used by the [Qt/QML Inspector](../inspector_qml/README.md),
[NUCLEO-H7S3L8 firmware](../nucleo_h7s3l8/README.md) and their host tests.
It is not another Inspector application, library component or build target.

For the desktop executable, build `examples/inspector_qml/CMakeLists.txt` with
`INSPECTOR_ENABLE_SERIAL=ON` and the documented external COBS checkout. For the
device, use `examples/nucleo_h7s3l8/Build.ps1`. Neither project copies this profile;
both include the same header. COBS itself remains an external dependency.
