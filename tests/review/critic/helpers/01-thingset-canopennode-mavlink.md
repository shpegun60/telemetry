# Helper 01: ThingSet, CANopenNode, MAVLink

A background general-purpose research agent launched by the critic reviewer on 2026-09-25 at about
21:54 local machine time. Task description: "Research ThingSet, CANopenNode, MAVLink". Web sources in its report were
checked on 2026-09-25, during its run. It marks each fact with its own evidence tags, defined at the
top of its report (V = confirmed on a fetched official page, S = search snippet only, I = inference
from verified facts, M = from memory, not checked on the web).

Sections (a) and (b) were copied by a script directly from the helper's own session transcript, so
they are exact: nothing was retyped or edited. The harness had indented every line of the report by
two spaces when it relayed the report; that indentation is not part of the helper's text and is not
reproduced here.

- (a) brief: 3177 characters, SHA-256 1872166e66894bfc82456b315c5c9471012bc389e24d2ddb2c69436b6f128987
- (b) report: 27603 characters, SHA-256 5578506be8456b9b5c59070d08d054b254949d4e82ef68dd2add69f9f9e2eb44

## (a) The brief, verbatim

~~~~text
You are doing market research on public embedded data-model / telemetry / parameter libraries. Use WebSearch and WebFetch (load them with ToolSearch "select:WebSearch,WebFetch" if they are not loaded) against OFFICIAL sources: the project's own documentation site, its GitHub README/source, or its published specification. Search only for the public library names and their features. Do not write any files. Do not run any build.

Libraries to research (three):
1. ThingSet (Libre Solar) - the ThingSet protocol specification (thingset.io) and the ThingSet Node C library / Zephyr SDK (github.com/ThingSet, e.g. thingset-node-c, thingset-zephyr-sdk).
2. CANopenNode (github.com/CANopenNode/CANopenNode, v4 object dictionary, CANopenEditor for EDS/XDD, storage module).
3. MAVLink parameter protocol, command protocol and component metadata protocol (mavlink.io microservices, and the MAVLink generators / pymavlink / QGroundControl).

For EACH library, fill these capability rows. For every row give: a verdict (yes / no / partial), one short line of detail, and the URL of the page that supports it. If you could not confirm a row on the web, say "unverified (from memory)" explicitly instead of a URL.
 1. Typed values (which scalar types)
 2. Metadata carried by the device or its description: units, limits (min/max), defaults, enums, flags (list each of the five separately: yes/no)
 3. Commands / RPC / functions / methods
 4. Schema / metadata discovery at runtime from the device (can a generic client learn the data model from the device itself?)
 5. ID stability across firmware versions: how are IDs assigned (numeric registry, index/subindex, names, hashes, positions)? What happens when an item is inserted or removed?
 6. Name/path addressing (can an item be addressed by name or path?)
 7. Text encoding(s) and binary encoding(s) on the wire
 8. Streaming / periodic publication / subscriptions / change notification
 9. Persistence of settings (built-in store/restore mechanism?)
10. Access control (authentication, per-item read/write rights, access levels)
11. Strings, arrays, structs/records, blobs support
12. Host tools: client libraries, code generators, GUI tools
13. Transports provided (serial, CAN, BLE, UDP/TCP, etc.)
14. Build systems and packaging (CMake, Zephyr module, PlatformIO, Arduino, ESP-IDF, pip/npm packages, etc.)
15. RTOS/platform integration (Zephyr, FreeRTOS, bare metal, Linux)
16. Footprint: any published Flash/RAM figures (quote them with the source) - otherwise "no published figure"
17. Safety / coding-standard claims (MISRA C, certification, static analysis claims)
18. License
19. Maturity and community: first release year, latest release version and date, approximate GitHub stars/contributors, notable users

Also report for each: implementation language and standard (e.g. C99, C++17).

Return a compact structured report (plain text or markdown tables) with all rows for all three libraries, with URLs. Be precise and conservative: prefer "partial" with an explanation over an optimistic "yes". Mark clearly which cells you verified on a fetched page and which are from memory. Deliver the report with SubagentHandback.
~~~~

## (b) The complete report, verbatim and unabridged

<!-- BEGIN VERBATIM REPORT -->
# Capability comparison: ThingSet, CANopenNode, MAVLink parameter/command/metadata

**Evidence key:**
- **V**: confirmed on a page I fetched.
- **S**: seen only in a search-result snippet of an official page (the page was too large to fetch whole).
- **M**: from memory, not checked on the web.
- **I**: my inference from confirmed facts.

**Caveat on "V":** the fetch tool summarises pages, so "V" means the extraction reported it, not that I read every line. For every claim that matters I asked for verbatim quotes. One summariser invention was caught this way: it said CANopenNode's objectDictionary.md documents XDD `unit`, `minValue`, `maxValue` and `allowedValues`. A verbatim re-check shows none of those strings is in the file.

**How "flags" is read in row 2:** named bit-field descriptions, plus per-item attribute flags where a library has them.

---

## 1. ThingSet (spec v0.6, thingset-node-c, thingset-zephyr-sdk)

**Language:**
- thingset-node-c is written in C. The C standard revision is not stated, and the library uses Zephyr kernel calls (`k_sem_take`) (T13, V).
- The spec is language-neutral.
- Parsers: JSMN for JSON and zcbor for CBOR (T17, V).

| # | Row | Verdict | Detail | Evidence |
|---|---|---|---|---|
| 1 | Typed values | yes | node-c types: bool, u8, i8, u16, i16, u32, i32, u64, i64, f32, decfrac, string, bytes, arrays. There is no f64. 64-bit, decfrac and bytes are each switched on by Kconfig. The spec itself only says "numbers, strings and booleans", with base-64 binary. | T12, T16, T1 (V) |
| 2a | Units | yes | The unit is part of the item name (`rVoltage_V`), so the device carries it. A `unit` metadata key covers non-ASCII units such as °C. | T1, T2 (V) |
| 2b | Limits | partial | `min`/`max` exist only in `_Metadata`, which "would usually not be stored inside a constrained device" and is served from the JSON at `cMetadataURL`. | T2 (V) |
| 2c | Defaults | no | There is no default key. The `_Metadata` keys are title, unit, min, max, enum, flags. | T2 (V) |
| 2d | Enums | partial | An `enum` key exists, but only in the external `_Metadata` JSON. | T2 (V) |
| 2e | Flags | partial | A `flags` key (bit positions to names) exists in the external `_Metadata`. The item class travels only as a name-prefix convention: c, r, w, s, p, t, o, x. | T1, T2 (V) |
| 3 | Commands | yes | Executable items (prefix `x`) are called with EXEC (`!` in text, 0x02 in binary); the parameters are the item's child items. node-c returns either nothing (FN_VOID) or an int32 (FN_I32). Type strings look like `(u32,string)->(i32)`. | T3, T4, T12, T13 (V) |
| 4 | Runtime discovery | yes / partial | FETCH with a null payload lists child names one level at a time (`?Bat null`), in text and binary. `_Ids`/`_Paths` map between IDs and paths. node-c's optional `_Metadata` endpoint (CONFIG_THINGSET_METADATA_ENDPOINT) returns only name and type, e.g. `{"name":"wF32","type":"f32[]"}`: no access rights, no limits. | T4, T5, T15, T16, T14 (V) |
| 5 | ID stability | explicit numeric IDs | The developer assigns 16-bit IDs, unique per device. 0x00, 0x10–0x1F and IDs from 0x8000 up are reserved. The spec says nothing about stability across firmware versions (V). Inserting an item renumbers nothing (I). On import, unknown IDs are silently skipped ("silently ignore this item"). Re-using an ID for a different item means raising `THINGSET_STORAGE_DATA_VERSION`, which throws away all stored data; the help text says "Try to avoid changing data object IDs used previously". | T1, T13, T19 (V) |
| 6 | Name/path addressing | yes | Hierarchical paths such as `Device/xAuth` work in text mode and in binary mode ("can either use data object IDs or names / paths"). | T4, T5 (V) |
| 7 | Encodings | text + binary | Text: JSON with one-character request codes `? = + - ! # @` and `:85`-style status replies. Binary: CBOR, keyed by IDs or by names. On CAN, a single-frame report puts the raw 16-bit ID in the CAN identifier. | T4, T5, T7 (V) |
| 8 | Streaming | partial | Reports are unconfirmed and may be broadcast. Subsets: `a` sent on request, `e` "only published if changed/updated", `m` "in regular time intervals". The `_Reporting` overlay has `sEnable`, `sPeriod_s` and rate limits. node-c implements REPORT in text and binary-ID modes; DESIRE and binary-name REPORT are not done yet. No per-client subscription found. Whether the code actually publishes on change: not established. | T1, T2, T6, T17, T22 (V) |
| 9 | Persistence | yes (SDK only) | The SDK stores the `TS_SUBSET_NVM` items in EEPROM (with CRC, optional double write) or Flash/NVS. It can save on every update and/or on a timer (1–8760 h, default 6 h). A 2-byte data-version header is checked, and an overwrite can be blocked after a failed load. node-c itself only provides export/import as ID/value CBOR. | T19, T20 (V) |
| 10 | Access control | partial | Each item has read and write bits for three roles: user, expert, manufacturer. Failures answer Unauthorized (0xA1) or Forbidden (0xA3). You log in through an `xAuth` executable; the SDK holds tokens for expert and manufacturer. "The password is transferred as a plain text string. Encryption has to be provided by lower layers." | T12, T15, T21, T4, T3 (V) |
| 11 | Strings, arrays, structs, blobs | yes | UTF-8 strings in a fixed buffer. Bytes (base64 in text mode). Arrays with element type, maximum and current count. Records (arrays of key/value records, addressed by index). Groups give nesting. | T1, T12 (V) |
| 12 | Host tools | partial | ThingSet App (Flutter): "generic user interface for any device supporting the ThingSet protocol", over BLE or WebSocket. python-thingset (Brill Power): PyPI 0.5.1 of 2026-09-04, with a `thingset` CLI over TCP, CAN/ISO-TP, serial and UDP. ThingSet.Net and thingsetplusplus are 2025 forks. There is no code generator: items are C macros (`THINGSET_ADD_*`) gathered into linker sections. | T24, T25, T26, T18 (V) |
| 13 | Transports | yes | The spec defines Serial, WebSocket, CAN, BLE, with mappings for MQTT, CoAP and LoRaWAN. CAN uses ISO-TP, J1939-style addressing, address claiming, and CAN FD at 2 Mbit/s. SDK docs cover Serial (UART/USB CDC-ACM), CAN, BLE and LoRaWAN. The SDK source also has `websocket.c` and `wifi.c`, but its docs call MQTT and WebSocket-over-WiFi "under development". | T8, T7, T23 (V) |
| 14 | Build and packaging | partial | node-c is a Zephyr module only: west.yml pins Zephyr v4.0-branch plus zcbor, and its CMake uses `zephyr_*` calls. The older predecessor library had a PlatformIO `library.json`. The Python client is on PyPI. No Arduino, ESP-IDF or npm package found. | T18, T29, T25 (V) |
| 15 | RTOS / platform | Zephyr only | "currently requires Zephyr RTOS. It can be used in other embedded C environments with minor changes." No FreeRTOS or bare-metal port found. | T17 (V) |
| 16 | Footprint | no published figure | The only numbers are target classes: the spec aims at RFC 7228 classes C0–C2 (C0: RAM << 10 KiB, Flash << 100 KiB). Those are targets, not measurements. | T9 (V) |
| 17 | Safety / coding standard | none claimed | No MISRA or certification claim found. Unit tests use Zephyr ztest, and a coverage report is published. | T17 (V) |
| 18 | License | Apache-2.0 | node-c, the SDK, the app and python-thingset are Apache-2.0. The SDK docs are CC BY-SA 4.0. | T17, T23, T25 (V) |
| 19 | Maturity | small | The spec repo dates from 2016-07-29, with spec versions v0.1–v0.6; v0.6 is meant to be "the last breaking changes... before finalizing v1.0"; no dates are given. node-c: created 2023-04-25, no releases or tags, 10 stars, 7 forks, 5 contributors, last push 2026-09-23. SDK: 22 stars, 16 forks, 10 contributors, no tags. Users: Libre Solar BMS and charge-controller firmware (V). Brill Power is a user (I, from its contributors and the forks). | T26, T27, T11, T28 (V) |

**ThingSet URLs**
- T1 https://thingset.io/spec/v0.6/data_model/structure.html
- T2 https://thingset.io/spec/v0.6/data_model/overlays.html
- T3 https://thingset.io/spec/v0.6/protocol/functions.html
- T4 https://thingset.io/spec/v0.6/protocol/text_mode.html
- T5 https://thingset.io/spec/v0.6/protocol/binary_mode.html
- T6 https://thingset.io/spec/v0.6/protocol/connectivity.html
- T7 https://thingset.io/spec/v0.6/transports/can.html
- T8 https://thingset.io/spec/v0.6/introduction/abstract
- T9 https://thingset.io/spec/v0.6/introduction/objectives.html
- T11 https://thingset.io/spec/changelog
- T12 https://raw.githubusercontent.com/ThingSet/thingset-node-c/main/include/thingset.h
- T13 https://raw.githubusercontent.com/ThingSet/thingset-node-c/main/src/thingset_bin.c (also thingset.c and thingset_txt.c)
- T14 https://raw.githubusercontent.com/ThingSet/thingset-node-c/main/tests/protocol/src/txt.c
- T15 https://raw.githubusercontent.com/ThingSet/thingset-node-c/main/src/thingset_common.c
- T16 https://raw.githubusercontent.com/ThingSet/thingset-node-c/main/Kconfig.thingset
- T17 https://github.com/ThingSet/thingset-node-c , https://thingset.io/software/node_library , https://thingset.io/thingset-node-c/
- T18 https://raw.githubusercontent.com/ThingSet/thingset-node-c/main/CMakeLists.txt and /west.yml
- T19 https://raw.githubusercontent.com/ThingSet/thingset-zephyr-sdk/main/src/Kconfig.storage
- T20 https://raw.githubusercontent.com/ThingSet/thingset-zephyr-sdk/main/src/storage_flash.c
- T21 https://raw.githubusercontent.com/ThingSet/thingset-zephyr-sdk/main/src/Kconfig.auth
- T22 https://raw.githubusercontent.com/ThingSet/thingset-zephyr-sdk/main/include/thingset/sdk.h
- T23 https://thingset.io/thingset-zephyr-sdk/ , https://github.com/ThingSet/thingset-zephyr-sdk/tree/main/src
- T24 https://github.com/ThingSet/thingset-app
- T25 https://github.com/Brill-Power/python-thingset , https://pypi.org/pypi/python-thingset/json
- T26 https://api.github.com/orgs/ThingSet/repos?per_page=100
- T27 https://api.github.com/repos/ThingSet/thingset-node-c (with /tags, /releases, /contributors)
- T28 https://libre.solar/software/bms.html
- T29 https://github.com/ThingSet/thingset-device-library

---

## 2. CANopenNode (v4 object dictionary, CANopenEditor, storage module)

**Language:**
- The README says "written in ANSI C in object-oriented way". C99/C11 are not mentioned (C1, V).
- CANopenEditor is C#/.NET 8 (C10, V).

| # | Row | Verdict | Detail | Evidence |
|---|---|---|---|---|
| 1 | Typed values | partial | The CANopen types (BOOLEAN, INTEGER8–64, UNSIGNED8–64, REAL32/64, VISIBLE/OCTET/UNICODE_STRING, DOMAIN, TIME_OF_DAY, TIME_DIFFERENCE) live in the EDS/XDD. The compiled OD keeps only a data pointer, a byte length and attribute bits (ODA_MB marks multi-byte). The device does not hold a type code. | C2, C3 (V) |
| 2a | Units | no | CANopenEditor's EDS model has no unit field (V). CiA 306 EDS has no unit key (M). XDD units: unverified. | C11 (V), M |
| 2b | Limits | partial | EDS LowLimit/HighLimit are in the description only. The OD structures have no min/max fields; limits can only be enforced by application code in an OD extension. | C11, C3 (V), I |
| 2c | Defaults | partial | The EDS DefaultValue / XDD `defaultValue` become the initialisers in OD.c. Writing 'load' to 0x1011 restores them. A client cannot read a default as metadata. | C2, C5 (V), I |
| 2d | Enums | no | No enumeration concept in the OD docs (the string "enumeration" is absent from objectDictionary.md). | C2 (V); EDS has none (M) |
| 2e | Flags | partial | Per-entry attribute bits on the device: ODA_SDO_R/W, TPDO/RPDO/SRDO-mappable, MB, STR. The EDS has ObjFlags. No named bit-field descriptions. | C3, C11 (V); M |
| 3 | Commands | partial | There is no RPC primitive. Actions are writes to OD entries that OD-extension write callbacks handle, e.g. 0x1010 'save' and 0x1011 'load'. NMT start/stop/preop/reset. No return value beyond SDO abort codes (I). | C3, C5, C7 (V) |
| 4 | Runtime discovery | no / partial | The compiled OD has no names or types (V). A client can only probe index/subindex with SDO reads (M). Standard object 0x1021 "Store EDS" appears in CANopenNode's OD table with no module using it, so embedding the EDS is left to the application (I). | C2, C3 (V) |
| 5 | ID stability | explicit index/subindex | 16-bit index plus 8-bit subindex, chosen by the designer, so inserting an entry moves nothing (I). Standard index ranges (M). Persistence stores raw storage-group struct images guarded by a signature of 16-bit length plus CRC16. If the group size changes, the entry is marked corrupt and defaults stay in effect (V). A re-layout that keeps the same size would load wrong values (I). | C2, C6 (V) |
| 6 | Name/path addressing | no | Only index/subindex, on the wire and in the ASCII gateway (`r <index> <subindex> <type>`). Names exist only in the EDS/XDD and as generated C identifiers such as `OD_PERSIST_COMM.x1000_deviceType`. | C2, C7 (V) |
| 7 | Encodings | binary + ASCII gateway | CAN frames (SDO, PDO) are binary. The CiA 309-3 ASCII command interface provides text. Description files are EDS (INI) and XDD (XML). | C1, C7, C10 (V) |
| 8 | Streaming | yes | TPDO transmission: synchronous (every N SYNC), acyclic, or event-driven with event timer and inhibit time. `OD_requestTPDO()` sends on application request; the doc says the application "may... monitor change of state" itself. Dynamic and bitwise mapping, 8 mapped entries by default. Heartbeat, EMCY and TIME producers. | C4, C1 (V) |
| 9 | Persistence | yes | 0x1010 'save' / 0x1011 'load' (subindex 1 all, 2 comm, 3 application, 4–127 manufacturer). Storage groups are set in the editor (`CO_storageGroup`), with cmd/auto/restore attributes. The EEPROM backend checks CRC16; in auto mode it writes changed bytes as they change. CANopenLinux uses files. | C5, C6, C2, C12 (V) |
| 10 | Access control | partial | Per-entry read/write and mappability attributes; errors such as READONLY/WRITEONLY. No authentication or roles in CANopen (M). | C3 (V), M |
| 11 | Strings, arrays, structs, blobs | yes | VISIBLE_STRING, OCTET_STRING, UNICODE_STRING. DOMAIN is streamed through the OD extension. ARRAY and RECORD objects, bounded by the 8-bit subindex (I). | C2, C3 (V) |
| 12 | Host tools | yes | CANopenEditor: GUI plus EDSSharp CLI. It imports EDS, XDD v1.0/v1.1 and XDC, and exports EDS/XDD/XDC/DCF, CANopenNode v3/v4 OD.c/OD.h, HTML/Markdown docs and a network PDO report. It runs on .NET 8 Windows, a cross-platform GUI is in progress; GPL-3.0, about 265 stars. CANopenLinux provides `canopend` and the `cocomm` CiA 309-3 client. CANopenDemo. | C10, C12, C1 (V) |
| 13 | Transports | CAN only | Classic CAN. CANopen FD (CiA 1301) is not implemented: issue #342 (2021) is closed, and a Zephyr RFC of 2026-04-03 treats FD as future work on top of v4. The ASCII gateway runs over stdio or a local socket (V); TCP (M). | C16, C15, C12 (V) |
| 14 | Build and packaging | partial | The core repo has no top-level build system; I found no CMakeLists or Makefile listed. Ports supply their own: CANopenLinux uses a Makefile, CanOpenSTM32 is "tied to the CubeMX configuration", Zephyr uses the external CANopenNodeZephyr module, ESP-IDF has a community port. No official PlatformIO or Arduino package. | C1, C12, C13, C14, C9 (V) |
| 15 | RTOS / platform | broad | Bare metal and FreeRTOS: STM32 examples, bxCAN and FDCAN, including STM32H7. Linux socketCAN, single- or multi-threaded with a 1 ms real-time thread. PIC32/dsPIC, ADI MAX32, Mbed OS, ESP32. Zephyr is the weak spot: the Zephyr RFC says the module "is based on v1.3", and the glue repo's sample still uses the legacy CO_OD.c/CO_OD.h. | C9, C13, C12, C15, C14 (V) |
| 16 | Footprint | no published figure | The README says "Suitable for 16-bit microcontrollers and above". Issue #302 (2021) asked for ROM/RAM figures; the fetched page shows none. | C1, C17 (V) |
| 17 | Safety / coding standard | claims made | "conform to the MISRA C:2012 guidelines, with some noted exceptions". Checked with PC-lint Plus; about 21 rule deviations are listed; OD.c/OD.h, the ASCII gateway and the fifo are excluded. "CANopen Conformance Test Tool passed." It implements CANopen Safety (EN 50325-5: SRDO/GFC), but no certification claim was found. | C8, C1 (V) |
| 18 | License | Apache-2.0 | Apache-2.0 on GitHub; it was LGPL on SourceForge. CANopenEditor is GPL-3.0. | C18, C19, C10 (V) |
| 19 | Maturity | high | On SourceForge since 2004-06-28, on GitHub since 2015. Tags: v0.5 2015-07-25, v4.0 2020-10-07, v2.0 2020-12-28, v4.1 2025-11-22; GitHub Releases lists only v1.0–v1.3. About 2011 stars, 806 forks, last push 2026-07-10. Contributor count not retrieved (API refused). Adopters: Analog Devices MSDK port, Zephyr module. | C18, C19, C9 (V) |

**CANopenNode URLs**
- C1 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/README.md
- C2 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/doc/objectDictionary.md
- C3 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/301/CO_ODinterface.h
- C4 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/301/CO_PDO.h
- C5 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/storage/CO_storage.h
- C6 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/storage/CO_storageEeprom.c (and .h)
- C7 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/309/CO_gateway_ascii.h
- C8 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/MISRA.md
- C9 https://raw.githubusercontent.com/CANopenNode/CANopenNode/master/doc/deviceSupport.md
- C10 https://github.com/CANopenNode/CANopenEditor
- C11 https://raw.githubusercontent.com/CANopenNode/CANopenEditor/main/libEDSsharp/eds.cs
- C12 https://github.com/CANopenNode/CANopenLinux
- C13 https://github.com/CANopenNode/CanOpenSTM32
- C14 https://docs.zephyrproject.org/latest/develop/manifest/external/canopennode.html , https://github.com/zephyrproject-rtos/CANopenNodeZephyr/tree/main/samples/canopennode/objdict
- C15 https://github.com/zephyrproject-rtos/zephyr/issues/106828
- C16 https://github.com/CANopenNode/CANopenNode/issues/342
- C17 https://github.com/CANopenNode/CANopenNode/issues/302
- C18 https://api.github.com/repos/CANopenNode/CANopenNode , https://github.com/CANopenNode/CANopenNode/tags
- C19 https://sourceforge.net/projects/canopennode/

---

## 3. MAVLink (parameter, command and component metadata protocols; generators, pymavlink, QGroundControl)

**Language:**
- The reference C library generated by mavgen is header-only; the C standard revision is not stated (M13, V).
- mavgen/pymavlink are Python, 3.9–3.14 per PyPI (M16, V).
- MAVSDK is C++ (M18, V); C++17 (M).

| # | Row | Verdict | Detail | Evidence |
|---|---|---|---|---|
| 1 | Typed values | yes / partial | Message fields: int8–int64, uint8–uint64, float, double, char, and arrays of these. Parameters: MAV_PARAM_TYPE covers 8–64-bit integers and 32/64-bit floats, but the value travels in a 4-byte float field (byte-wise or C-cast). 64-bit values cannot fit losslessly (I). The parameter metadata schema only allows the types Uint8 to Int32 and Float. | M10, M1, M5 (V) |
| 2a | Units | yes | XML fields carry `units`; parameter metadata carries `units`. | M10, M5 (V) |
| 2b | Limits | yes | XML `minValue`/`maxValue`; parameter metadata `min`/`max`/`increment`. | M10, M5 (V) |
| 2c | Defaults | yes | XML `default`; parameter metadata `default`. | M10, M5 (V) |
| 2d | Enums | yes | XML `enum` references; parameter metadata `values`. | M10, M5 (V) |
| 2e | Flags | yes | Enums can be marked `bitmask="true"`. Parameter metadata has `bitmask`, `rebootRequired`, `volatile` and `readOnly`. | M9, M5 (V) |
| 2 (note) | Who holds the metadata | — | Parameter metadata comes from the device only if it implements component metadata (PX4 stores parameters.json.xz inside its binary). Message metadata lives in the XML compiled into both ends (I). | M17 (V) |
| 3 | Commands | yes | COMMAND_LONG (7 floats) or COMMAND_INT (params 5 and 6 as scaled integers, plus a frame). COMMAND_ACK carries a result, progress 0–100 and `result_param2`. MAV_RESULT_IN_PROGRESS and COMMAND_CANCEL exist. The only return payload is the result and progress. | M3 (V) |
| 4 | Runtime discovery | partial | PARAM_REQUEST_LIST returns every parameter's name, type, value, index and count. Rich metadata comes via COMPONENT_METADATA (message 397, requested with MAV_CMD_REQUEST_MESSAGE): a general.json served over MAVLink FTP (`mftp://`) or HTTP, with a CRC for caching, optional .xz compression and translations. The schemas are "work in progress", and the commands and events schemas are "TBD". Message layouts cannot be discovered from the device; CRC_EXTRA only detects a mismatch. | M1, M4, M6 (V) |
| 5 | ID stability | names plus fixed IDs | Parameters are keyed by name. The index is positional: "the mapping of param_index to a particular parameter might change on systems where parameters can be added/removed", and the parameter set must not change during a session. PX4 sends a hash (PARAM_HASH) so a ground station can check its cache. Message IDs are unique within a generated library, with ranges per dialect (common 300–10000). Released messages may only gain appended extension fields; CRC_EXTRA detects incompatible edits. Enum entries should carry explicit values. | M1, M9, M6 (V) |
| 6 | Name/path addressing | partial | Parameters use flat names of up to 16 characters. There is no hierarchy; `group` and `category` are UI metadata only. Messages and commands use numeric IDs. | M1, M5 (V) |
| 7 | Encodings | binary only | v1 starts with 0xFE, v2 with 0xFD. Little-endian, fields reordered by size, v2 drops trailing zero bytes, optional 13-byte signature. JSON is used only for the metadata files, XML for the definitions. | M6, M4 (V) |
| 8 | Streaming | yes | Telemetry is multicast. MAV_CMD_SET_MESSAGE_INTERVAL sets a rate per link (-1 disables, 0 restores the default); MAV_CMD_REQUEST_MESSAGE asks for one message. PARAM_VALUE is broadcast after a PARAM_SET, but cache sync is "not guaranteed". | M8, M12, M1 (V) |
| 9 | Persistence | partial | The protocol has MAV_CMD_PREFLIGHT_STORAGE (read or write persistent storage, reset to defaults; accepted only before flight) (S). The actual storage belongs to the flight stack (M). The library has none (I). | M19 (S) |
| 10 | Access control | partial | MAVLink 2 signing authenticates links: a SHA-256-based 48-bit signature, a 32-byte key, a timestamp and a link ID. No encryption. Unsigned packets are accepted by system-specific rules. No per-parameter or per-command rights in the protocol (V). Metadata has a `readOnly` hint (V). PARAM_ERROR has a READ_ONLY code (S). | M7, M5 (V); M20 (S) |
| 11 | Strings, arrays, structs, blobs | partial | Message fields can be fixed-size arrays and `char[]`. A message is one flat struct (I). Blobs go over MAVLink FTP, 239 data bytes per message. Parameters are scalars only; strings exist only in the extended parameter protocol, which is work in progress and aimed at cameras (128-byte value, custom type). | M10, M14, M2 (V) |
| 12 | Host tools | extensive | mavgen generates C, C++, Python and JavaScript, plus C#, Kotlin, TypeScript, Lua, Swift and Ada. Also rust-mavlink, gomavlib and a Java library. pymavlink 2.4.50 was uploaded 2026-09-24. MAVSDK: C++, BSD-3, about 936 stars. QGroundControl: about 4978 stars; it builds its parameter UI from metadata and bundles PX4's parameter XML as a fallback. MAVProxy and Mission Planner (M). | M11, M16, M18, M17 (V) |
| 13 | Transports | any byte stream | UART and UDP examples are confirmed; TCP (M). Rates are set per link. | M12, M8 (V) |
| 14 | Build and packaging | yes | CMake install with `find_package(MAVLink)` and the `MAVLink::mavlink` target (header-only). pymavlink via pip. Prebuilt c_library_v2 repo and Rust crate (M). | M15, M16 (V) |
| 15 | RTOS / platform | neutral | The header-only C library needs only a send function you provide. By default "up to 16 channels" on Windows, Linux and macOS and "up to 4" elsewhere. Flight stacks: PX4 on NuttX, ArduPilot on ChibiOS/Linux (M). | M13 (V) |
| 16 | Footprint | no published Flash/RAM figure | Only wire overhead is published: "MAVLink 1 has just 8 bytes overhead per packet". Minimum packet is 12 bytes for v2; payload is at most 255 bytes. | M13, M11, M6 (V) |
| 17 | Safety / coding standard | none found | Integrity checks only: CRC-16/MCRF4XX plus CRC_EXTRA. | M12 (V) |
| 18 | License | MIT / LGPLv3 | Generated libraries and XML definitions are MIT. The generator toolchain (pymavlink) is LGPLv3. Docs are CC BY 4.0. MAVSDK is BSD-3. QGroundControl shows Apache-2.0 on GitHub; dual licence with GPLv3 is from memory (M). | M11, M16, M18 (V) |
| 19 | Maturity | very high | "first released early 2009 by Lorenz Meier". mavlink/mavlink repo: created 2011-08-12, 2441 stars, 2259 forks, no GitHub releases (rolling master). FTP is implemented "(at least) in PX4, ArduPilot, QGroundControl and MAVSDK". Contributor count not retrieved. | M11, M15, M14 (V) |

**MAVLink URLs**
- M1 https://mavlink.io/en/services/parameter.html
- M2 https://mavlink.io/en/services/parameter_ext.html
- M3 https://mavlink.io/en/services/command.html
- M4 https://mavlink.io/en/services/component_metadata.html
- M5 https://raw.githubusercontent.com/mavlink/mavlink/master/component_metadata/parameter.schema.json
- M6 https://mavlink.io/en/guide/serialization.html
- M7 https://mavlink.io/en/guide/message_signing.html
- M8 https://mavlink.io/en/guide/message_rates.html
- M9 https://mavlink.io/en/guide/define_xml_element.html
- M10 https://mavlink.io/en/guide/xml_schema.html
- M11 https://mavlink.io/en/
- M12 https://mavlink.io/en/about/overview.html
- M13 https://mavlink.io/en/mavgen_c/
- M14 https://mavlink.io/en/services/ftp.html
- M15 https://github.com/mavlink/mavlink , https://api.github.com/repos/mavlink/mavlink
- M16 https://pypi.org/project/pymavlink/ , https://pypi.org/pypi/pymavlink/2.4.50/json
- M17 https://docs.px4.io/main/en/advanced/px4_metadata
- M18 https://api.github.com/repos/mavlink/qgroundcontrol , https://api.github.com/repos/mavlink/MAVSDK
- M19 https://raw.githubusercontent.com/mavlink/mavlink/master/message_definitions/v1.0/common.xml (search snippet only)
- M20 https://github.com/PX4/PX4-Autopilot/blob/main/src/modules/mavlink/mavlink_parameters.cpp (search snippet only)

---

## Findings that matter for a comparison

- **Metadata served by the device itself:**
  - ThingSet node-c serves only name and type. Limits, enums and flags sit in an external JSON; there are no defaults.
  - CANopenNode serves none. The metadata lives in EDS/XDD files, which carry limits and defaults but no units or enums.
  - MAVLink parameters get the full set (units, min/max, default, enum values, bitmask, readOnly), but only through component metadata files, in practice from PX4.
- **Stored settings versus changing IDs:**
  - ThingSet stores values keyed by ID and silently skips unknown IDs, so adding and removing items is tolerated. Re-using an ID needs a version bump that discards everything stored.
  - CANopenNode stores raw struct images checked by length and CRC, so any size change falls back to defaults.
  - MAVLink says the parameter index may move, and clients must use names.
- **Only CANopenNode makes a coding-standard claim:** MISRA C:2012 with listed deviations, checked with PC-lint Plus, plus a conformance-tool pass.
- **Zephyr only:** ThingSet runs only on Zephyr. CANopenNode's Zephyr module is the legacy v1.3 generation; its STM32 port, with bare-metal and FreeRTOS examples, is the maintained one.
- **Not published anywhere I looked:** Flash/RAM footprint figures for any of the three.

<!-- END VERBATIM REPORT -->

## (c) What the critic rejected, corrected or did not use, and why

Used: every row of all three tables became the ThingSet (TS), CANopenNode (CO) and MAVLink (MV)
columns of the capability matrix; the per-cell lines and URLs are in ../Comparison.txt.

Corrected or changed by me, and why:
1. ThingSet maturity. The helper wrote "small". My first hand-back matrix said "low-mid".
   The helper's evidence (node-c: no releases, about 10 stars, 5 contributors) supports "small";
   the complete report uses "small". This corrects my cell, not the helper.
2. MAVLink transports. The helper wrote "any byte stream (UART/UDP examples; TCP from memory)".
   My first hand-back matrix said "Y". MAVLink defines framing but ships no link drivers, so the
   complete report uses "P".
3. MAVLink metadata. The helper notes the full set reaches a client only when the device
   implements the component metadata protocol (in practice PX4). The matrix keeps the device-served
   marking but states that condition.
4. CANopen defaults. The helper wrote "partial": EDS defaults become initialisers, and a client
   cannot read them as metadata. The matrix marks them as held in the description file only.
5. My first hand-back said that "ThingSet, LwM2M and CANopen keep limits and enums in external
   description files". That was inaccurate for CANopen: the helper found no enumeration concept in
   the CANopenNode object-dictionary documentation and no unit field in CANopenEditor's EDS model.
   The complete report says CANopen's EDS holds limits and defaults but no units or enums.

Not used in the report (kept here for later analysis):
- Star, fork and tag counts beyond the one-word maturity verdicts.
- CANopenNode's list of ports and drivers, and its CiA 309-3 gateway syntax.
- MAVLink details: the PARAM_HASH cache check, FTP payload size, the 16-channel default and the
  full list of generator languages (summarised as "10+ languages").
- The helper's own caught extraction error about CANopenNode XDD fields; it had already
  corrected it, so nothing needed doing.

Cells the helper marked as not web-verified (they stay marked in ../Comparison.txt):
- From memory (M): CiA 306 EDS has no unit key; CANopen has no authentication or roles; standard
  CANopen index ranges; MAVLink over TCP; QGroundControl dual licence; MAVProxy and Mission Planner;
  MAVSDK being C++17; the prebuilt c_library_v2 and Rust crate.
- Search snippet only (S): MAV_CMD_PREFLIGHT_STORAGE persistence; the PARAM_ERROR READ_ONLY code.
- Inference (I): several ID-stability consequences (for example that inserting an entry moves
  nothing), which follow from the verified ID schemes.

## (d) Usage and outcome

210,504 tokens; 148 tool calls; 2,140,089 ms (about 35.7 minutes). Status: finished normally and delivered one report. It did not fail, time out or return nothing.
Figures are from the harness's completion notice for this helper.
