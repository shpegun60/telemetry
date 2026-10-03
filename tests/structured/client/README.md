# Stage 12: structural JS client and Qt example

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

[web/telemetry.js](../../../web/telemetry.js) is the final v3 payload codec.
It requires BigInt/DataView support; CI uses Node 22. It imports no transport,
resource protocol or Bind/Exchange. The former v2 decoder is retired.

## Client API

```js
import * as ts from './telemetry.js';

const model = ts.parseDescriptor(descriptorBytes);
const service = model.services[0];
const payload = ts.encodeServiceRequest(model, service.id, requestObject);
// An application adapter transmits payload and handles statuses/correlation.
const response = ts.decodeServiceResponse(model, service.id, responseBytes);
const values = ts.decodeValues(model, valuesBytes);
```

Public structural rows use `typeId` for Fields and `requestTypeId` plus
`responseTypeId` for Services. Commands have `requestTypeId`. Type rows expose
scalar codes, enum entries, struct members and array element/count metadata.
`encodeValue/decodeValue` operate directly on any validated registry TypeId;
`encodeFieldWrite/decodeFieldValue/encodeCommandRequest` provide named routing.
Unknown packed IDs are rejected at u32 width. Void is JavaScript `undefined`.

Parsed models/rows/arrays are immutable; lookup Maps and the model identity
check are private. A cloned/hand-written object is not a validated model.
DescriptorCache validates each newly supplied byte sequence, returns the
cached model for identical bytes and refuses equal-hash/different-byte input.
It has bounded capacity and no session state. Fingerprint comparison for
values uses the cached descriptor fingerprint; live values are never hashed.

Scalar representations are bool, Number for integers up to 32 bits/F32/F64,
and BigInt for U64/S64. Integer range/fractional errors are rejected; F32 is
rounded to binary32, with finite overflow rejected. NaN/Inf and negative zero
are supported. A decoded JS NaN does not promise its original payload bits;
retain the raw bytes for an exact NaN bit-pattern roundtrip. Enum dictionaries
label values without restricting representable unknown codes. Structs decode
to objects with a null prototype; encoding requires all and only their named
members. Arrays require exactly the declared length.

Descriptor parsing verifies v3.0, all reserved bytes and record versions,
section ordering/count arithmetic, positional IDs, backward non-Void type
references, wire sizes, request/response shape and strict UTF-8/NUL rules.
Resource ceilings match the default C++ profile and may be tightened by
`parseDescriptor(bytes, { typeDepth: 16, ... })`. Counts/lengths are checked
before record-list/string allocation; input size is checked before copying.
Values checks header identity, exact dense token length/status, unavailable
zero payloads and canonical bool representations.

## Reproduce

```sh
python3 tests/structured/client/run.py --cxx g++ --node node --build-dir build/client
python3 tests/structured/client/run.py --cxx g++ --null-checks --build-dir build/client-null
python3 tests/structured/client/run.py --cxx clang++-18 --sanitize --build-dir build/client-san
python3 -m venv build/client-venv
build/client-venv/bin/pip install playwright==1.63.0
build/client-venv/bin/python -m playwright install --with-deps chromium
build/client-venv/bin/python tests/structured/client/run.py --browser --build-dir build/client-browser
```

Windows venv paths use `Scripts/python.exe`. For an existing browser set
`TELEMETRY_BROWSER_EXECUTABLE` to its executable; the same Playwright tests
then run against that browser. Generated fixtures/logs/images remain in the
chosen build directory. Node and Playwright are desktop test dependencies,
not dependencies of the MCU library.

## Evidence and scope

- `Check.mjs`: **3057 counted checks**, including every truncated prefix of
  three independently frozen descriptors, semantic mutations with recomputed
  fingerprints, resource/depth/expansion bounds, literal/prototype names,
  scalar extrema, unknown enums, arrays/structs/Void and values errors.
  A valid model at the default descriptor ceiling reaches group/entry 65535
  and packed ID `0xffffffff` without signed JS bitwise narrowing.
- `Device.cpp`: C++ generates descriptor, values and native request/response
  bytes. JS reads them and sends its own payloads through real encoded Field,
  Command and Service indexes. All application statuses, unavailable target,
  exact callback counts and wrong payload length are checked. It uses the
  example's existing library ABI/Workspace, not a second JS-aware endpoint.
- `Resources.cpp`: the client also consumes the unchanged Stage 10 fixture.
  Both newly emitted C++ descriptors pass the independent Python oracle.
- `Browser.py`: **20 counted checks** in a real browser, including a recursive
  form, exact downloaded bytes, U64/S64/unknown enum display, every application
  status, Void/unavailable, fingerprint mismatch, literal HTML-like text, and
  a delayed operation that executes once despite the client timeout.
- `QtSmoke.cpp`: Qt 6.10.1 MinGW C++20 smoke passed with `-Werror`, exact 41-byte
  response, U64/S64 preserved and native readAs using the same model.

The final-path preview on 2026-10-03 passed the same 3057 JS checks and 20
Chromium browser checks, plus the Qt smoke, against the migrated C++ API.

The host runner executes five successful commands (six with the browser);
those are command counts, separate from the assertions above. Local MinGW
13.1, GCC 13.3 with null checks and Clang 18 ASan/UBSan runs passed. The original Stage 12 browser run used Edge through
Playwright 1.63.0; the final-path preview and CI use pinned Chromium. The
original UI rendering was inspected
from `client-desktop.png`. The test-only optional protocol remains covered by
its existing correlation/wrap/disconnect suite, not made mandatory here.

This is desktop correctness/integration evidence, not new MCU timing.
Stage 12 originally changed no library C++/qmake input, wire format or endpoint
ABI. The final migration uses `<telemetry/Telemetry.hpp>`, namespace
`telemetry`, and `resource/telemetry/v3` providers; it keeps the same native
DTOs and canonical bytes. Whole-program stack and MCU cycle evidence is
retained separately in [Stage 14 qualification](../mcu/h7s/README.md).
