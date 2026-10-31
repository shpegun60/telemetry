# Runnable examples

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

Choose an application here before selecting its build project. The repository's
root `CMakeLists.txt` builds and installs the library, while `telemetry.pro`
builds the separate [Qt Widgets playground](../app/README.md). The Universal
Inspector has its own CMake project under `inspector_qml`.

Run commands in each linked README from the Telemetry repository root unless
that README explicitly changes directory. Put generated SDKs, build directories
and test receipts outside the source checkout. Qt, the STM32 SDK and COBS are
required only by the examples that explicitly select them.

## Start with a complete program

| Task | Example and build entry point |
| --- | --- |
| Local Native Field, Command and Service | [User-guide programs](user_guide/README.md); start with `NativeOnly.cpp`, no Qt or transport |
| Integrate your device class and receive complete requests | [Device integration](device_integration/README.md), `device_integration/CMakeLists.txt` |
| Inspect or transactionally edit unknown DTO bytes offline | [Runtime schema](runtime_schema/README.md); standalone reader/writer commands |
| Call unknown runtime endpoints | [DynamicClient](dynamic_client/README.md), `dynamic_client/CMakeLists.txt` |
| Compare individual, aggregate and Resource reads before ReadMany | [Read comparison](read_comparison/README.md), `read_comparison/CMakeLists.txt` |
| Use a portable read-only model without Qt | [InspectorModel](inspector/README.md), `inspector/CMakeLists.txt` |
| Open the Universal Inspector window | [Qt/QML Inspector](inspector_qml/README.md), `inspector_qml/CMakeLists.txt` |
| Edit a Field and read back its applied value | [FieldWrite Workbench](inspector_qml/README.md#read-edit-write-and-refresh), same Qt/QML project; choose Writable demo |
| Connect that window to NUCLEO-H7S3L8 over UART/DMA | [Board firmware](nucleo_h7s3l8/README.md), `nucleo_h7s3l8/Build.ps1`; enable serial in the Inspector project |

The Inspector starts with an in-process Demo device. TCP can also be exercised
with its loopback demo server. UART requires Qt SerialPort, the documented COBS
revision and separately built device firmware. Importing a Descriptor file alone
provides metadata browsing, not live device values.

## Generated SDKs and clients

| Example | What to build |
| --- | --- |
| [Generated C++](codegen_cpp/README.md) | `codegen_cpp/CMakeLists.txt` generates the SDK and builds the standalone, Encoded and RPC programs |
| [Generated Encoded](codegen_encoded/README.md) | Build the `codegen_encoded` target through the C++ project above |
| [Generated RPC](codegen_rpc/README.md) | Build the `codegen_rpc` target through the C++ project above |
| [Generated Qt/QML](codegen_qml/README.md) | `codegen_qml/CMakeLists.txt`; a device-specific generated API, distinct from the Universal Inspector |
| [Generated web](codegen_web/README.md) | Follow its SDK generation and Node/browser commands |
| [Portable async client](async_client/README.md) | Bare-metal owner loop and FreeRTOS port examples; application supplies the backend and transport |
| [JS/TS async client](client_web/README.md) | Shared runtime, typed callbacks and application transport |

The [generator](../tools/README.md) is a Python host tool. Native API and a typed
client with an application Codec do not require a Generated SDK.

## Resource and transport integration

| Example | Purpose |
| --- | --- |
| [Resource providers](resources/README.md) | Generic custom files and providers |
| [COBS](cobs_integration/README.md) | Real COBS framing/pools on a simulated host port |
| [RPC mailbox](rpc_mailbox/README.md) | Server request/result lifecycle through Resource |
| [RPC client](rpc_client/README.md) | Low-level client packet engine and host peer |
| [RPC sessions](rpc_multi_session/README.md) | Multiple independent server mailbox sessions |
| [Structured client](structured_client/README.md) | Descriptor/codec browser and Qt consumers |
| [Bind/Exchange](structured_protocol/README.md) | Optional application-owned agreement example |
| [Shared Inspector serial profile](inspector_serial/README.md) | Header used by both Inspector and Nucleo; not a separate executable |

Reusable transport-neutral adapters belong to [Integration](../lib/integration/README.md).
Qt sockets, GUI objects and board HAL configuration stay in their application
examples. See [modules](../doc/Modules.md) for CMake/qmake selection and
[maintained tests](../tests/README.md) for qualification commands.
