# Helper 03: Zephyr settings + MCUmgr/SMP, Pigweed, nanopb (plus Memfault, Glaze, reflect-cpp)

A background general-purpose research agent launched by the critic reviewer on 2026-09-25 at about
21:54 local machine time. Task description: "Research Zephyr MCUmgr, Pigweed, nanopb". Web sources in its report were
checked on 2026-09-25, during its run. It marks each fact with its own evidence tags, defined at the
top of its report (V = confirmed on a fetched official page, S = search snippet only, I = inference
from verified facts, M = from memory, not checked on the web).

Sections (a) and (b) were copied by a script directly from the helper's own session transcript, so
they are exact: nothing was retyped or edited. The harness had indented every line of the report by
two spaces when it relayed the report; that indentation is not part of the helper's text and is not
reproduced here.

- (a) brief: 3819 characters, SHA-256 ca92ec006c47d4d07a05ea65312ed52fdec6747a92859c2767bef461ed048c55
- (b) report: 27380 characters, SHA-256 59e84422d3b0832f7b2be2511c08d82577b4e13f94bfbea96805bacd83f64707

## (a) The brief, verbatim

~~~~text
You are doing market research on public embedded data-model / telemetry / parameter libraries. Use WebSearch and WebFetch (load them with ToolSearch "select:WebSearch,WebFetch" if they are not loaded) against OFFICIAL sources: the project's own documentation site, its GitHub README/source, or its published specification. Search only for the public library names and their features. Do not write any files. Do not run any build.

Libraries to research (three, plus two short extras):
1. Zephyr settings subsystem plus MCUmgr / SMP (docs.zephyrproject.org: settings API, SMP protocol, the settings management group, statistics management group (stats), OS management; host tools mcumgr CLI, smpmgr, smpclient, nRF Connect Device Manager).
2. Pigweed (pigweed.dev): pw_metric (tokenized metric names), pw_tokenizer, pw_rpc (with codegen and host clients, pw_rpc console), pw_protobuf, pw_kvs / pw_persistent_ram, and the build systems supported (GN, Bazel, CMake).
3. nanopb (github.com/nanopb/nanopb, jpa.kapsi.fi/nanopb) as the representative of schema-first protobuf on MCUs: field numbers as stable IDs, .proto options, codegen, footprint figures published in its docs.
Extras (just 3-5 lines each, no full matrix): (a) Memfault metrics (heartbeat metrics definition, keys, SDK footprint claims, license); (b) Glaze (stephenberry/glaze) and reflect-cpp (getml/reflect-cpp): does each generate JSON Schema from C++ structs, support binary formats, work without exceptions/RTTI, and what C++ standard does each need.

For EACH of the three main libraries, fill these capability rows. For every row give: a verdict (yes / no / partial), one short line of detail, and the URL of the page that supports it. If you could not confirm a row on the web, say "unverified (from memory)" explicitly instead of a URL.
 1. Typed values (which scalar types)
 2. Metadata carried by the device or its description: units, limits (min/max), defaults, enums, flags (list each of the five separately: yes/no)
 3. Commands / RPC / functions / methods
 4. Schema / metadata discovery at runtime from the device (can a generic client learn the data model from the device itself?)
 5. ID stability across firmware versions: how are IDs assigned (numeric field numbers, names, hashes/tokens, positions)? What happens when an item is inserted or removed?
 6. Name/path addressing (can an item be addressed by name or path?)
 7. Text encoding(s) and binary encoding(s) on the wire
 8. Streaming / periodic publication / subscriptions / change notification
 9. Persistence of settings (built-in store/restore mechanism?)
10. Access control (authentication, per-item read/write rights, access levels)
11. Strings, arrays, structs/records, blobs support
12. Host tools: client libraries, code generators, GUI tools
13. Transports provided (serial, CAN, BLE, UDP/TCP, etc.)
14. Build systems and packaging (CMake, Bazel, GN, Zephyr module, PlatformIO, Arduino, ESP-IDF, pip/npm packages, etc.)
15. RTOS/platform integration (Zephyr, FreeRTOS, bare metal, Linux)
16. Footprint: any published Flash/RAM figures (quote them with the source) - otherwise "no published figure"
17. Safety / coding-standard claims (MISRA C, certification, static analysis, fuzzing claims)
18. License
19. Maturity and community: first release year, latest release version and date, approximate GitHub stars/contributors, notable users

Also report for each: implementation language and standard (e.g. C99, C++17).

Return a compact structured report (plain text or markdown tables) with all rows for all three libraries plus the short extras, with URLs. Be precise and conservative: prefer "partial" with an explanation over an optimistic "yes". Mark clearly which cells you verified on a fetched page and which are from memory. Deliver the report with SubagentHandback.
~~~~

## (b) The complete report, verbatim and unabridged

<!-- BEGIN VERBATIM REPORT -->
# Market research: embedded data-model, telemetry and parameter libraries

**Legend:** [V] = confirmed on a fetched official page. [S] = seen only in an official-site search snippet. [I] = my inference from verified facts. [M] = unverified (from memory).
Source tags (Z#, P#, N#) resolve to the URL lists under each table.

**Caveats:**
- pigweed.dev rendered pages came back as navigation only through the fetcher. The Pigweed rows therefore cite the same docs' RST and source files in the official google/pigweed GitHub mirror.
- GitHub shows no year for releases in the current year, so those dates are taken as 2026.
- Nothing was written to disk and nothing was built.

## Quick comparison
- **Device-carried metadata:** none of the three carries units, limits or flags on the device. Only nanopb (protobuf) has defaults and enums, and those live in the `.proto` file on the host.
- **Runtime discovery:**
  - Zephyr is partial: it lists groups and stat names, but settings keys cannot be listed over SMP.
  - Pigweed has none: tokens and name-hash IDs need the host's token database and `.proto` files.
  - nanopb has none: "Reflection ... is not supported".
- **ID schemes:**
  - Zephyr uses name strings for settings and fixed numbers for SMP groups and commands.
  - Pigweed uses 32-bit hashes of names (28-bit for metric names).
  - nanopb uses explicit field numbers. It is the only one where you choose the IDs yourself and renaming is free.
- **Streaming:** only Pigweed has it (pw_rpc streaming). Zephyr SMP is request/response only. nanopb is a codec.
- **Persistence:** Zephyr settings has it built in, with SMP load/save/commit. Pigweed has pw_kvs as a separate module. nanopb has none.
- **Access control:** only Zephyr has hooks (a per-key settings hook, a command hook, DTLS and BLE permissions). Pigweed and nanopb have none.

---

## 1. Zephyr settings + MCUmgr/SMP (+ stats)

**Language:** C. The codebase requires C99 features, and the docs recommend "at least the C17 standard" [V Z17].

| # | Row | Verdict | Detail | Src |
|---|---|---|---|---|
| 1 | Typed values | no (settings) / partial (stats) | A setting value is `const void *value, size_t val_len`; no type is stored. `SETTINGS_MAX_VAL_LEN` is 256. The SMP settings group sends values as CBOR bstr: "the underlying data type cannot be specified through this and must be known by the client". Stats are unsigned 16/32/64-bit counters (`STATS_SECT_ENTRY16/32/64`), sent as CBOR unsigned. | Z2 Z3 Z5 Z6 Z7 [V] |
| 2 | Metadata | units **no**; limits **no**; defaults **no**; enums **no**; flags **no** | Not described in the settings docs or the SMP settings/stats groups. Defaults exist only in application code (the doc example is `DEFAULT_FOO_VAL_VALUE`) and are not exposed. | Z1 Z5 Z6 [V] |
| 3 | Commands | yes | SMP groups:<br>• OS: echo, task stats, memory-pool stats, date-time, reset, MCUmgr params, OS/app info, bootloader info<br>• image, FS, settings, stats, enumeration, Zephyr basic<br>• shell: runs a shell command and returns `"o"` text plus `"ret"`<br>Application groups use ID ≥ 64, and their payload need not be CBOR. | Z4 Z8 Z9 [V] |
| 4 | Runtime discovery | partial | • The enumeration group lists group IDs, with optional names and handler counts.<br>• The stats group has "list groups" and "group data" (a name→value map).<br>• The settings group has **no** key-listing command.<br>• `settings list [subtree]` exists in the settings shell, so it is reachable as text through the shell group [I].<br>No types or units can be discovered. | Z10 Z6 Z5 Z16 [V] |
| 5 | ID stability | strings (settings); fixed numbers (SMP); names or positions (stats) | • **Settings:** keys are `/`-separated name strings. Adding or removing a key does not affect the others; a rename creates a new key [I]. The ZMS backend hashes names, with configurable collision bits (`CONFIG_SETTINGS_ZMS_MAX_COLLISIONS_BITS`).<br>• **SMP:** group and command IDs are fixed numbers (0–63 reserved, 64+ for applications).<br>• **Stats:** names are stored only with `CONFIG_STATS_NAMES`; otherwise names are generated as `s<stat-idx>`. That makes them positional, so inserting an entry shifts the later ones [I]. | Z1 Z3 Z4 Z7 [V] |
| 6 | Name/path addressing | yes | Settings are addressed by full name, with subtree load/save (`settings_load_subtree`, `settings_save_subtree`). Maximum depth is 8; `SETTINGS_MAX_NAME_LEN` = 8*8 = 64. Stats are addressed by group name. | Z1 Z2 Z3 [V] |
| 7 | Encodings | binary CBOR; text only as transport framing | • An 8-byte big-endian SMP header plus a CBOR payload.<br>• Raw UART is binary.<br>• The console and shell transports carry frames as Base64 with CRC16 and a 127-byte frame limit.<br>• No JSON. | Z4 Z12 [V] |
| 8 | Streaming/subscriptions | no | Only client-initiated request/response is documented. BLE uses GATT notifications only as the response channel. Settings has no change notification to clients; `h_set`/`h_commit` are callbacks inside the firmware. | Z4 Z27 Z12 Z1 [V] |
| 9 | Persistence | yes | `settings_save` / `_save_one` / `_save_subtree` and `settings_load` / `_load_subtree`. Backends are `CONFIG_SETTINGS_NVS` and `_ZMS` (both recommended), `_FCB` and `_FILE`. Several read sources are allowed, with one write destination. SMP group 3 exposes commit, load and save. | Z1 Z5 [V] |
| 10 | Access control | partial | SMP itself has no authentication.<br>• **UDP:** `MCUMGR_TRANSPORT_UDP_DTLS` means "unauthenticated connections are disabled".<br>• **BLE:** `MCUMGR_TRANSPORT_BT_PERM_RW_ENCRYPT` / `_RW_AUTHEN`.<br>• **App hooks:** `CONFIG_MCUMGR_GRP_SETTINGS_ACCESS_HOOK` allows or denies per key. `CONFIG_MCUMGR_SMP_COMMAND_STATUS_HOOKS` (`MGMT_EVT_OP_CMD_RECV`) can reject any command, for example with `MGMT_ERR_EACCESSDENIED`.<br>There are no built-in users or access levels. | Z13 Z14 Z15 [V] |
| 11 | Strings/arrays/structs/blobs | partial | A setting is a blob of at most 256 B, so it can hold any serialized struct or string. Custom SMP groups can carry any CBOR (strings, arrays, maps). Stats are scalars only. | Z3 Z4 Z6 [V/I] |
| 12 | Host tools | yes (no codegen) | From Zephyr's own table:<br>• AuTerm (Qt GUI, GPL-3.0)<br>• mcumgr-client (Rust)<br>• mcumgr-web (browser, MIT)<br>• nRF Connect Device Manager (Android/iOS, BLE only; has `SettingsManager` and `StatsManager`; `mcumgr-ble` 3.4.1)<br>• smp, smpclient and smpmgr (Python, Apache-2.0; smpmgr 0.19.1 of 2026-09-07 works over serial/BLE/UDP)<br>• mcumgr-toolkit (Rust/Python)<br>• the in-tree Zephyr MCUmgr client (C)<br>The legacy Go mcumgr CLI ("a thin wrapper over the Apache newtmgr tool", 84 stars) is not in Zephyr's table. | Z11 Z23 Z24 Z25 [V] |
| 13 | Transports | yes | UART (console encoding), raw UART, shell, BLE GATT, UDP IPv4/IPv6 (default port 1337, MTU 1500, optional DTLS), LoRaWAN, SPI, and dummy transports for tests. USB is available as a CDC-ACM flavour of the sample [S]. No CAN transport is listed. | Z11 Z12 Z14 [V] Z26 [S] |
| 14 | Build/packaging | Zephyr only | In-tree subsystems built with CMake, Kconfig and west; not packaged for other build systems [I]. Host tools are on PyPI (smp, smpclient, smpmgr) and Maven (`no.nordicsemi.android:mcumgr-ble`). | Z11 Z23 Z25 [V] |
| 15 | RTOS/platform | Zephyr only | The protocol descends from Apache Mynewt's newtmgr. For DFU, "Currently only the MCUboot bootloader is supported". | Z11 Z24 [V] |
| 16 | Footprint | no published figure | None found on the settings or MCUmgr pages or the smp_svr sample. The only numbers are config defaults: UDP thread stack 1024 B, MTU 1500 B, serial frame 127 B. | Z1 Z11 Z12 Z14 [V] |
| 17 | Safety/coding standard | partial, project-wide | The coding guidelines are "based on MISRA-C 2012 and are a subset" (147 MISRA-derived rules plus 5 project rules). The safety committee targets IEC 61508 SIL 3 / SC 3 "for a limited source scope (see certification scope TBD)"; nothing is certified yet. Nothing specific to settings or MCUmgr. | Z18 Z19 [V] |
| 18 | License | Apache-2.0 | Host tools are mostly Apache-2.0; AuTerm is GPL-3.0 and mcumgr-web is MIT. | Z20 Z11 [V] |
| 19 | Maturity | high | • Announced 2016-02-17 [S]; the settings API says "Since 1.12".<br>• Latest release is Zephyr 4.4.2, 2026-08-07.<br>• 16.6k stars, 10.0k forks; "more than 3,000 contributors" and "more than 1000 boards" (Linux Foundation, 2026-03-04).<br>• Nordic maintains nRF Connect Device Manager; nRF Connect SDK is Zephyr-based [M]. | Z3 Z20 Z21 [V] Z22 [S] |

**Zephyr sources**
- Z1 https://docs.zephyrproject.org/latest/services/storage/settings/index.html
- Z2 https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/include/zephyr/settings/settings.h
- Z3 https://docs.zephyrproject.org/latest/doxygen/html/group__settings.html
- Z4 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_protocol.html
- Z5 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_groups/smp_group_3.html
- Z6 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_groups/smp_group_2.html
- Z7 https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/include/zephyr/stats/stats.h
- Z8 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_groups/smp_group_0.html
- Z9 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_groups/smp_group_9.html
- Z10 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_groups/smp_group_10.html
- Z11 https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html
- Z12 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_transport.html
- Z13 https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr_callbacks.html
- Z14 https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/subsys/mgmt/mcumgr/transport/Kconfig.udp
- Z15 https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/subsys/mgmt/mcumgr/transport/Kconfig.bluetooth
- Z16 https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/subsys/settings/src/settings_shell.c
- Z17 https://docs.zephyrproject.org/latest/develop/languages/c/index.html
- Z18 https://docs.zephyrproject.org/latest/contribute/coding_guidelines/index.html
- Z19 https://docs.zephyrproject.org/latest/safety/safety_overview.html
- Z20 https://github.com/zephyrproject-rtos/zephyr and https://github.com/zephyrproject-rtos/zephyr/releases/latest
- Z21 https://www.linuxfoundation.org/press/zephyr-turns-10-as-global-adoption-surges-and-long-term-embedded-use-expands
- Z22 https://www.linuxfoundation.org/press/press-release/the-linux-foundation-announces-project-to-build-real-time-operating-system-for-internet-of-things-devices
- Z23 https://github.com/intercreate/smpmgr, https://pypi.org/project/smpmgr/, https://github.com/intercreate/smpclient
- Z24 https://github.com/apache/mynewt-mcumgr-cli
- Z25 https://github.com/NordicSemiconductor/Android-nRF-Connect-Device-Manager
- Z26 https://docs.zephyrproject.org/latest/samples/subsys/mgmt/mcumgr/smp_svr/README.html
- Z27 https://docs.zephyrproject.org/latest/services/device_mgmt/smp_groups/smp_group_1.html

---

## 2. Pigweed (pw_metric, pw_tokenizer, pw_rpc, pw_protobuf, pw_kvs, pw_persistent_ram)

**Language:** "All Pigweed code requires C++17 and is fully compatible with C++20 and C++23" [V P19]. pw_tokenizer also has C macros and a Rust API. Host tools are in Python, TypeScript and Java.

| # | Row | Verdict | Detail | Src |
|---|---|---|---|---|
| 1 | Typed values | yes | • pw_metric text says "The supported metric types are uint32_t, float, and uint64_t". Its API reference also lists `TypedMetric<int64_t / int32_t / bool / double / TokenValue>`, and the metric proto's value oneof has float, uint32, uint64, int64, bool, int32, double and bytes.<br>• pw_rpc and pw_protobuf use the proto3 scalar types.<br>• pw_kvs stores trivially copyable values or byte spans. | P1 P2 P10 P13 [V] |
| 2 | Metadata | units **no**; limits **no**; defaults **no**; enums **partial**; flags **no** | A metric holds only token, type and value; its initial value is a macro argument, not metadata. pw_protobuf "only supports targeting proto3", so there are no custom defaults. Enums exist only as `.proto` enums on the host. The device holds no names at all. | P1 P10 [V] |
| 3 | Commands | yes | pw_rpc supports unary, server-streaming, client-streaming and bidirectional-streaming calls. C++ code is generated for nanopb, pw_protobuf or raw. | P6 P7 [V] |
| 4 | Runtime discovery | no | Metric names are tokens, so the host needs the token database. RPC IDs are name hashes, and the host uses the shared `.proto` files [S P28]. No reflection service was found. `MetricService.Walk` lists metric instances (token path plus value), not a schema. | P1 P2 P9 [V] P28 [S] |
| 5 | ID stability | hashes of names | • RPC service and method IDs are `hash_65599` of the fully-qualified service name and of the method name.<br>• A metric name is the pw_tokenizer 32-bit hash ("a modified version of the x65599 hash"), kept as the lower 28 bits of `name_and_type_`.<br>• Because IDs do not depend on declaration order, inserting or removing items moves nothing; renaming changes the ID [I].<br>• Collisions: the docs give 50 % odds at about 77,000 strings for 32-bit tokens; the same bound gives about 19,000 for 28 bits [I, my calculation].<br>• Token databases can mark removed strings with a date.<br>• pw_kvs rejects a key whose hash collides (`AlreadyExists`). | P9 P3 P1 P4 P13 [V] |
| 6 | Name/path addressing | partial | Metrics are addressed by `token_path` (repeated fixed32); the `string_path` field is "currently unsupported". RPC is addressed by name hash, pw_kvs by string key. | P2 P13 [V] |
| 7 | Encodings | binary; Base64 text for tokens | RPC packets are protobuf. pw_hdlc provides framing with CRC-32. Tokenized strings are binary or Base64 with a `$` prefix (e.g. `$HL2VHA==`). JSON is not established for these modules. | P8 P15 P5 [V] |
| 8 | Streaming | partial | RPC streams in both directions. `MetricService.Get` is a "Server-streaming RPC to send all registered metrics to the caller in batches"; the paginated unary `Walk` is "the recommended method". No built-in periodic publication, subscription or change notification for metrics. | P1 P2 P7 [V] |
| 9 | Persistence | partial | Provided by separate modules, not wired to metrics:<br>• pw_kvs: a log-structured flash key-value store with wear leveling and optional redundant copies; it "improves robustness against unexpected power loss".<br>• pw_persistent_ram: `Persistent<T>` with CRC16. It survives a soft reboot, though this is "not guaranteed"; it does not survive power loss and is not double-buffered. | P12 P13 P14 [V] |
| 10 | Access control | no | The pw_rpc docs and design page say nothing about authentication or per-item rights. | P6 P7 [V, by absence] |
| 11 | Strings/arrays/structs/blobs | yes (proto) / no (metrics) | pw_protobuf handles string, bytes, repeated, nested messages, `std::optional` scalars, and oneof via callbacks. Its options are `max_size`, `max_count`, `fixed_size`, `fixed_count` and `use_callback`. Maps are not mentioned. pw_metric values are scalar only. | P10 P1 [V] |
| 12 | Host tools | yes | • pw_rpc clients in C++, Python, TypeScript and Java.<br>• pw_console: a terminal REPL and log viewer with "interactive RPC" over pw_hdlc; no GUI.<br>• pw_web: an npm web log viewer, with web apps talking pw_rpc over Web Serial [S].<br>• Detokenizers in Python, C++ and TypeScript (Java via JNI).<br>• Codegen as protoc plugins (pwpb, nanopb, raw).<br>• Token database tools (CSV, binary, directory). | P6 P16 P5 P4 [V] P26 [S] |
| 13 | Transports | partial | Transport-agnostic: "arbitrary serial, bus, or packet transports (UART, SPI, USB, BLE, Sockets)". Pigweed ships HDLC framing (pw_hdlc); the physical links are up to the integrator [I]. | P6 P15 [V] |
| 14 | Build/packaging | GN, Bazel, CMake | • "GN is the most full-featured, followed by CMake, and finally Bazel", and GN is used for upstream work.<br>• SEED-0111 (accepted 2023-09-26) makes Bazel the recommended build system.<br>• "CMake will be supported indefinitely at the current level".<br>• Also: a Zephyr module via `CONFIG_PIGWEED_*` Kconfig [S], Soong [S], npm `pigweedjs` [S]. pip packaging is not established. | P17 P18 [V] P27 [S] |
| 15 | RTOS/platform | yes | • pw_thread backends: FreeRTOS, ThreadX, embOS, and STL for the host.<br>• Zephyr: the Zephyr page says "native Zephyr backends for several of its core OS abstraction layers", but the pw_thread page still lists Zephyr as planned.<br>• CMSIS-RTOS v2 / RTX5 is planned.<br>• Hosts: Linux and macOS, with Windows having "more sharp edges". Bare metal [M]. | P20 P21 P19 [V] |
| 16 | Footprint | partial | • pw_metric on 32-bit targets: UntypedMetric 8 B, `TypedMetric<uint32_t/float>` 12 B, `<uint64_t/int64_t>` 16 B, Group 16 B. The docs add that "the above sizes show an unexpectedly large flash impact".<br>• pw_tokenizer case study: "Log contents shrunk by over 50%, even with Base64 encoding", and firmware images were reduced "by up to 18%".<br>• pw_protobuf and pw_kvs have generated size reports; I could not capture their numbers.<br>• pw_rpc: no published figure. | P1 P3 P11 P12 [V] |
| 17 | Safety/coding standard | no claims | No MISRA or certification claim found. pw_fuzzer supports FuzzTest (recommended) and libFuzzer and "produces artifacts for ... ClusterFuzz and OSS-Fuzz". The SDK blog mentions Clang Thread Safety Analysis and hardening. | P22 P23 [V] |
| 18 | License | Apache-2.0 | | P25 [V] |
| 19 | Maturity | medium-high | • Announced 2020-03-19; the Pigweed SDK developer preview came in 2024-08.<br>• No numbered releases found ("live at HEAD" appears only in a search snippet [S]).<br>• GitHub mirror: 538 stars, 127 forks, ~16.9k commits. The canonical repo is pigweed.googlesource.com, so the star count understates use [I].<br>• Users: "shipped in millions of devices, including Google's own Pixel suite of devices, Nest thermostats, DeepMind robots, as well as satellites and autonomous aerial drones". | P24 P23 P25 [V] |

**Pigweed sources** (P1–P22 are RST/source files under `https://raw.githubusercontent.com/google/pigweed/main/`, the sources of pigweed.dev)
- P1 pw_metric/docs.rst
- P2 pw_metric/pw_metric_proto/metric_service.proto
- P3 pw_tokenizer/tokenization.rst
- P4 pw_tokenizer/token_databases.rst
- P5 pw_tokenizer/detokenization.rst
- P6 pw_rpc/docs.rst
- P7 pw_rpc/design.rst
- P8 pw_rpc/protocol.rst
- P9 pw_rpc/py/pw_rpc/ids.py
- P10 pw_protobuf/docs.rst
- P11 pw_protobuf/size_report.rst
- P12 pw_kvs/docs.rst
- P13 pw_kvs/public/pw_kvs/key_value_store.h
- P14 pw_persistent_ram/docs.rst
- P15 pw_hdlc/docs.rst
- P16 pw_console/docs.rst
- P17 docs/sphinx/build/overview.rst
- P18 seed/0111.rst
- P19 docs/sphinx/overview.rst
- P20 pw_thread/docs.rst
- P21 docs/sphinx/os/zephyr/index.rst
- P22 pw_fuzzer/docs.rst
- P23 https://opensource.googleblog.com/2024/08/introducing-pigweed-sdk.html
- P24 https://opensource.googleblog.com/2020/03/pigweed-collection-of-embedded-libraries.html
- P25 https://github.com/google/pigweed
- P26 https://pigweed.dev/pw_web/
- P27 https://pigweed.dev/docs/os/zephyr/kconfig.html
- P28 https://pigweed.dev/pw_rpc/
- P29 seed/0103.rst

---

## 3. nanopb (schema-first protobuf)

**Language:** "Pure C runtime" for "ANSI-C compatible compilers", which must provide `stdint.h`, `stdbool.h`, `stddef.h`, `string.h` and `limits.h` [V N2]. The generator is Python.

| # | Row | Verdict | Detail | Src |
|---|---|---|---|---|
| 1 | Typed values | yes | Supports "all data types" of protobuf; the individual type list is standard protobuf [M]. `PB_WITHOUT_64BIT`, `PB_CONVERT_DOUBLE_FLOAT` and `int_size` (e.g. `IS_8`) shrink them. | N2 N4 [V] |
| 2 | Metadata | units **no**; limits **no**; defaults **yes**; enums **yes**; flags **no** | • Defaults: "default values" are supported and `pb_decode()` applies them. This is proto2 syntax; proto3 has none [M].<br>• Enums: `.proto` enums, plus the optional `enum_to_string` ("can take up lots of space") and `enum_validate`.<br>• `max_size` / `max_count` bound storage, not value ranges.<br>• All of this lives in the `.proto` on the host; the firmware keeps only field descriptors. | N2 N3 N5 [V] |
| 3 | Commands | no | A serializer only. That nanopb generates nothing for `.proto` services is from memory [M]. A third-party nanogrpc exists [S], and pw_rpc can use nanopb structs [V]. | P6 [V] N13 [S] |
| 4 | Runtime discovery | no | "Reflection (runtime introspection) is not supported". Descriptors hold tags and types, not names. | N2 N3 [V] |
| 5 | ID stability | yes, explicit field numbers | A field number "cannot be changed once your message type is in use"; "Adding new fields is safe"; a deleted field's number "must" be reserved. Inserting or removing fields renumbers nothing, and renaming is wire-compatible [I]. The `msgid` option gives user-defined message-type IDs. | N11 N5 [V] |
| 6 | Name/path addressing | no | No names in the firmware; enum names only with `enum_to_string`. | N3 N5 [V] |
| 7 | Encodings | binary only | Protobuf wire format; numeric arrays are always packed. JSON and text formats are not among the features; I found no explicit statement either way. | N2 [V] |
| 8 | Streaming | no | Codec only. `pb_istream_t` / `pb_ostream_t` callbacks can encode straight to a UART or socket, and field callbacks handle messages larger than RAM, but there is no publication model. | N2 N3 [V] |
| 9 | Persistence | no | Nothing built in; an application could stream-encode into flash [I]. | — |
| 10 | Access control | no | The documented input model: nanopb "will never read more than bytes_left bytes" and "will never write more than max_size bytes" on untrusted input. | N6 [V] |
| 11 | Strings/arrays/structs/blobs | yes | • string/bytes as static `max_size`, callback or malloc pointer<br>• repeated fields (`max_count` / `fixed_count`)<br>• nested messages, oneof as a union, extensions<br>• maps via generated entry types<br>Groups are not supported, and unknown fields are not preserved. | N2 N4 N5 [V] |
| 12 | Host tools | codegen only | `nanopb_generator.py` is on pip as `nanopb`, can call protoc itself, and has worked as a protoc plugin (`--nanopb_out`) since 0.2.3. Host peers can use any standard protobuf library [I]. No GUI. | N7 N8 [V] |
| 13 | Transports | none | Codec only. | N2 [V] |
| 14 | Build/packaging | broad | Makefile, CMake (`FindNanopb.cmake`), SCons (generator only), Bazel, Conan, Meson, PlatformIO, PyPI, vcpkg. Also Arduino (PlatformIO `nanopb-arduino`) and a Zephyr module (`zephyr_nanopb_sources()`). ESP-IDF is not listed. | N8 N10 [V] |
| 15 | RTOS/platform | any | Pure C with no OS dependency: "No malloc needed ... Optional malloc support" (`PB_ENABLE_MALLOC`). | N2 N4 [V] |
| 16 | Footprint | yes | • Docs: "Small code size (5--20 kB depending on processor and compilation options, plus any message definitions)" and "Small ram usage (typically ~1 kB stack, plus any message structs)". Using only the encoder or only the decoder halves the code size.<br>• Homepage: "tight (<10 kB ROM, <1 kB RAM) memory constraints".<br>• Counterpoint from Pigweed's SEED-0103: a nanopb-generated crash struct "clocked in at around 12KB --- several times larger than its encoded size", because of static max-size allocation. | N2 N1 P29 [V] |
| 17 | Safety/coding standard | partial | • Documented input-handling guarantees (row 10).<br>• "Nanopb fuzzer has also been integrated in Google's OSSFuzz platform"; the `oss-fuzz/projects/nanopb` directory exists.<br>• "Extensive set of tests"; optional `PB_VALIDATE_UTF8`.<br>• No MISRA or certification claim.<br>• No stable ABI between versions. | N6 N7 N12 N2 N4 [V] |
| 18 | License | zlib | | N1 N8 [V] |
| 19 | Maturity | high | First release nanopb-0.1.0 (2012-01-06); latest nanopb-0.4.9.2 (2026-08-24). 5.6k stars, 1.0k forks. Shipped as a Zephyr module and used as one of pw_rpc's C++ codegen backends. | N9 N8 N10 P6 [V] |

**nanopb sources**
- N1 https://jpa.kapsi.fi/nanopb/
- N2 https://jpa.kapsi.fi/nanopb/docs/ (raw: https://raw.githubusercontent.com/nanopb/nanopb/master/docs/index.md)
- N3 https://jpa.kapsi.fi/nanopb/docs/concepts.html
- N4 https://jpa.kapsi.fi/nanopb/docs/reference.html
- N5 https://raw.githubusercontent.com/nanopb/nanopb/master/generator/proto/nanopb.proto
- N6 https://jpa.kapsi.fi/nanopb/docs/security.html
- N7 https://jpa.kapsi.fi/nanopb/docs/whats_new.html
- N8 https://github.com/nanopb/nanopb (raw README.md)
- N9 https://raw.githubusercontent.com/nanopb/nanopb/master/CHANGELOG.txt
- N10 https://docs.zephyrproject.org/latest/services/serialization/nanopb.html
- N11 https://protobuf.dev/programming-guides/proto3/
- N12 https://github.com/google/oss-fuzz/tree/master/projects/nanopb
- N13 https://github.com/d21d3q/nanogrpc

---

## Extras

**(a) Memfault metrics** [V]
- **Definition:** heartbeat metrics are declared in `memfault_metrics_heartbeat_config.def` with `MEMFAULT_METRICS_KEY_DEFINE(key, type)`. Variants are `_WITH_RANGE`, `_WITH_SCALE_VALUE`, `_WITH_SESSION` and `MEMFAULT_METRICS_STRING_KEY_DEFINE`.
- **Types and interval:** Unsigned, Signed, Timer (ms) and String; the default heartbeat is 3600 s.
- **Keys:** "Memfault requires the symbol file to process metrics sent from your device", so key names are resolved in the cloud from the uploaded .elf.
- **Footprint (whole SDK):** "The typical compiled SDK size is 5KB of flash and requires 1.5KB of RAM."
- **License:** the Memfault License, BSD-like but "must only be used with the Memfault services and integrated with the Memfault server". 238 stars. Targets Cortex-M/-R and ESP32, bare metal or RTOS.
- Sources: https://docs.memfault.com/docs/mcu/metrics-api, https://docs.memfault.com/docs/mcu/introduction, https://raw.githubusercontent.com/memfault/memfault-firmware-sdk/master/LICENSE, https://github.com/memfault/memfault-firmware-sdk

**(b) Glaze** [V]
- **JSON Schema:** yes, `glz::write_json_schema<T>()`. Per-field description, minimum and maximum can be attached via `glz::json_schema`.
- **Formats:** "JSON, BEVE, BSON, CBOR, CSV, JSONB, MessagePack, TOML, YAML, EETF". The binary ones are BEVE, BSON, CBOR, MessagePack, EETF and JSONB.
- **Exceptions/RTTI:** "No exceptions (compiles with -fno-exceptions)" and "No runtime type information necessary (compiles with -fno-rtti)".
- **Standard:** "Requires C++23" (Clang 18+, GCC 13+, MSVC 14.50).
- **Other:** MIT; v9.0.0 released 24 Sep 2026; ~3.0k stars. The README has an "Embedded/Size Optimization" section.
- Sources: https://github.com/stephenberry/glaze, https://stephenberry.github.io/glaze/json-schema/, https://github.com/stephenberry/glaze/releases/latest

**(b) reflect-cpp** [V]
- **JSON Schema:** yes, `rfl::json::to_schema<T>()`, "currently supported for JSON only".
- **Binary formats:** Avro, BSON, Cap'n Proto, CBOR, Cereal, flexbuffers, msgpack, UBJSON and yas, plus Parquet.
- **Exceptions/RTTI:** errors come back as `rfl::Result`, but `.value()` "throws an exception, if rfl::Result<T> contains an error". The fetched docs claim neither `-fno-exceptions` nor `-fno-rtti` builds, so both are **not established**.
- **Standard:** C++20 (GCC 11.4+, Clang 14+, MSVC 17.8+).
- **Other:** MIT; v0.25.0 released 16 May 2026; ~1.9k stars.
- Sources: https://github.com/getml/reflect-cpp, https://rfl.getml.com/result/, https://github.com/getml/reflect-cpp/releases/latest
<!-- END VERBATIM REPORT -->

## (c) What the critic rejected, corrected or did not use, and why

Used: every row of all three tables became the Zephyr settings + MCUmgr (ZM), Pigweed (PW) and
nanopb (NP) columns of the capability matrix; the per-cell lines and URLs are in ../Comparison.txt.
The extras (Memfault, Glaze, reflect-cpp) are in the Extras section of ../Comparison.txt and in
the report's "do not copy" list (identity that depends on a host-side token database).

Corrected or changed by me, and why:
1. Zephyr typed values. The helper wrote "no (settings) / partial (stats)". My first hand-back
   matrix said "N"; the complete report shows both parts.
2. Pigweed enums and nanopb defaults and enums exist only in the host-side .proto files. The matrix
   marks them as description-file metadata, not device-served.

Not used in the report (kept here for later analysis):
- Pigweed SEED-0103's counterpoint that a nanopb-generated struct was about 12 KB, several times
  its encoded size.
- The pw_tokenizer case-study numbers, the Pigweed hash-collision estimate (the helper's own
  inference), and Zephyr's full host-tool list (summarised in the matrix).
- Zephyr's ZMS name-hash collision option and the unnamed-stats positional naming (noted in
  ../Comparison.txt, not in the report).

Cells the helper marked as not web-verified (they stay marked in ../Comparison.txt):
- From memory (M): nRF Connect SDK being Zephyr-based; Pigweed bare-metal support; the nanopb
  scalar type list; nanopb generating nothing for .proto services; proto3 having no custom defaults.
- Search snippet only (S): the Zephyr announcement date; the Pigweed Zephyr Kconfig module, Soong
  and the npm package; pw_web over Web Serial; the nanogrpc project; "live at HEAD".
- Caveat the helper stated: pigweed.dev pages came back as navigation only, so the Pigweed rows cite
  the RST and source files in the official GitHub mirror instead.

## (d) Usage and outcome

192,862 tokens; 122 tool calls; 1,609,373 ms (about 26.8 minutes). Status: finished normally and delivered one report. It did not fail, time out or return nothing.
Figures are from the harness's completion notice for this helper.
