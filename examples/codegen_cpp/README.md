# Generated C++ examples

Build and run all three examples from an external directory:

```sh
cmake -S examples/codegen_cpp -B /tmp/telemetry-codegen-example -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/telemetry-codegen-example -j2
ctest --test-dir /tmp/telemetry-codegen-example --output-on-failure
```

`codegen_export` saves a real binary Descriptor from the unchanged
[native example model](../structured_client/Device.hpp). Python generates its
SDK under the build directory. `codegen_cpp` compiles with only that SDK's
include directory and the C++20 standard library. It encodes a typed Service
request and demonstrates the Operation/ID/payload custom transport boundary.

`codegen_encoded` uses the native Encoded API. `codegen_rpc` uses the existing
RpcClient, Resource protocol and RpcMailbox. Their CMake targets keep those
dependencies separate. These are host loopbacks; they do not access a board.

For an already generated SDK, qmake can build the standalone program without Qt:

```sh
mkdir -p /tmp/telemetry-codegen-qmake
cd /tmp/telemetry-codegen-qmake
qmake /path/to/telemetry/examples/codegen_cpp/codegen_cpp.pro CODEGEN_SDK=/tmp/telemetry-codegen-example/generated
make -j2
./codegen_cpp
```

On Windows use the matching Qt MinGW compiler and `mingw32-make`; do not mix
the Qt MinGW and MSYS2 UCRT toolchains. See [the generated API](../../doc/GeneratedCppApi.md)
and [full qualification suite](../../tests/codegen/README.md).
