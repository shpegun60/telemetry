# Maintained host tools

The [Telemetry code generator](../doc/CodeGenerator.md) runs directly from the
repository root with Python 3.10 or newer:

```sh
python3 -m tools.telemetry_codegen inspect descriptor.bin
python3 -m tools.telemetry_codegen generate descriptor.bin --targets cpp,ts,js --output /tmp/device-sdk
python3 -m tools.telemetry_codegen generate descriptor.bin --targets cpp,ts,js --output /tmp/device-sdk --check
```

It requires no third-party Python package. Fetching is optional and separate
from offline generation. Generated outputs and build artifacts belong outside
the source tree. See [qualification](../tests/codegen/README.md).

The optional `--targets qml` emits a Qt 6 QML module over the unchanged C++ SDK.
It includes `cpp` automatically and supports application Encoded transports and
the existing `RpcClient`; see [QML API](../doc/GeneratedQmlApi.md) and its
[runnable example](../examples/codegen_qml/README.md).

[Source style](source_style/check.py) verifies maintained source comments,
guards and pinned clang-format output; [documentation navigation](documentation_navigation/check.py)
validates local Markdown links and heading anchors. They operate on the Git
inventory, including new untracked authored files.
