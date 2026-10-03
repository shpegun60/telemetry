# Structured client example

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This desktop example uses one C++ Model for Field, Command and Service, its
canonical descriptor, and a transport-independent JavaScript payload codec.
It does not require the optional Bind/Exchange example.

```text
C++ DTOs + methods
    -> Model / Descriptor / ValuesFile
    -> validated JS structural model
    -> recursive Service form
    -> canonical request payload
    -> example HTTP adapter -> C++ encoded index
    -> status + canonical response payload
    -> JS response decoder
```

The [Device.hpp](Device.hpp) declarations contain only names and bindings.
One `Config` is shared by a writable Field, a Command and a Service request
and response. Types and reflected member names reach the UI automatically.
The example also includes a Void Service and an empty FunctionSlot.
`multiplier` status decisions belong to the fake Device method; they are not
exported as limits/defaults in the descriptor.

## Run

Build and test the native helper and client first, from the repository root:

```sh
python3 tests/structured/client/run.py --cxx g++ --node node --build-dir build/client
python3 examples/structured_client/serve.py --device build/client/client-device --data-dir build/client --port 8080
```

Open `http://127.0.0.1:8080/examples/structured_client/` and press **Load fake
Device**. On Windows the helper is `client-device.exe`; put the selected
MinGW compiler's runtime DLL directory on PATH when running it separately.
The test runner handles that PATH for its child processes.

The page can also open a descriptor without any server-side device, save
request payloads, read response payloads and decode a matching values file.
In this mode the fake call button stays disabled. U64/S64 inputs are decimal
text converted directly to BigInt. Display strings do not change wire bytes.

The form starts with **local UI zeros**, including an enum code of zero when
it is representable. These are not descriptor defaults. Unknown enum codes
are accepted; named codes are suggestions. All reflected text uses textContent.
There are no units, application limits, defaults or per-member overlays.

The illustrative HTTP adapter uses `/service/<u32-id>`, a raw request body,
and `X-Dispatch-Status` / `X-Endpoint-Status` response headers. This routing
and the single outstanding call belong to the example, not the codec or
telemetry core. A timeout can occur after the Device executes: the page
reports an unknown outcome and does not repeat the operation. Changing
descriptor/service selection cancels the pending display; an old response
is never decoded through a newly loaded model.

This is a localhost fake, not a firmware HTTP server. Each request starts a
fresh native helper, so application state does not persist between requests.
The UI caps form expansion at 4096 nodes and whole-file uploads at 16 MiB;
larger streams need an application reader instead of this demonstration form.

## Qt smoke

```sh
mkdir -p build/client-qt
cd build/client-qt
qmake6 ../../examples/structured_client/qt.pro CONFIG+=release QMAKE_CXXFLAGS+=-Werror
make -j2
./structured_client_qt
```

The console smoke uses QByteArray payloads with the same Model, native
readAs and encoded Service. It verifies U64/S64 extremes and the exact 41-byte
response. Qt JSON is used only for a desktop summary, with integer64 strings.
It is not an MCU formatter or a different telemetry format.

Automated validation and dependency commands are in
[tests/structured/client](../../tests/structured/client/README.md).
