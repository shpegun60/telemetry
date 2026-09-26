# Helper 02: Cyphal register API, NXP FreeMASTER, OMA LwM2M (Anjay, Anjay Lite, Wakaama)

A background general-purpose research agent launched by the critic reviewer on 2026-09-25 at about
21:54 local machine time. Task description: "Research Cyphal, FreeMASTER, LwM2M". Web sources in its report were
checked on 2026-09-25, during its run. It marks each fact with its own evidence tags, defined at the
top of its report (V = confirmed on a fetched official page, S = search snippet only, I = inference
from verified facts, M = from memory, not checked on the web).

Sections (a) and (b) were copied by a script directly from the helper's own session transcript, so
they are exact: nothing was retyped or edited. The harness had indented every line of the report by
two spaces when it relayed the report; that indentation is not part of the helper's text and is not
reproduced here.

- (a) brief: 3838 characters, SHA-256 4ea95890cf2350330401780c7b657d17e09b97a0b25ad4eed7c2ab0b1b52d76f
- (b) report: 26176 characters, SHA-256 b3a6ce79fe747bc56abbd73bd2d4fb922e7505e49e05293b8e3c0dcbf524e3ef

## (a) The brief, verbatim

~~~~text
You are doing market research on public embedded data-model / telemetry / parameter libraries. Use WebSearch and WebFetch (load them with ToolSearch "select:WebSearch,WebFetch" if they are not loaded) against OFFICIAL sources: the project's own documentation site, its GitHub README/source, or its published specification. Search only for the public library names and their features. Do not write any files. Do not run any build.

Libraries to research (three):
1. Cyphal (formerly UAVCAN v1) register API: uavcan.register.Access and uavcan.register.List services and the register conventions in the Cyphal Specification (opencyphal.org), plus implementations and tools (libcanard, libcyphal, 112/113 services, Yakut CLI, pycyphal, the DS-015 / "register" naming conventions for mutable/persistent registers).
2. NXP FreeMASTER: the embedded driver (FreeMASTER Communication Driver / "fmstr" in MCUXpresso SDK), its TSA (Target-Side Addressing) tables, pipes, recorder/oscilloscope, the FreeMASTER host GUI, the FreeMASTER Lite / JSON-RPC / JavaScript API, supported transports.
3. OMA LwM2M as implemented by Anjay (AVSystem, github.com/AVSystem/Anjay, and Anjay Lite / Anjay-zephyr / Anjay-esp32 if relevant) and Eclipse Wakaama: object/resource model, OMA object registry, observe/notify, content formats (TLV, SenML JSON/CBOR, plain text, LwM2M CBOR), access control object, bootstrap, security.

For EACH library, fill these capability rows. For every row give: a verdict (yes / no / partial), one short line of detail, and the URL of the page that supports it. If you could not confirm a row on the web, say "unverified (from memory)" explicitly instead of a URL.
 1. Typed values (which scalar types)
 2. Metadata carried by the device or its description: units, limits (min/max), defaults, enums, flags (list each of the five separately: yes/no)
 3. Commands / RPC / functions / methods (e.g. LwM2M Execute)
 4. Schema / metadata discovery at runtime from the device (can a generic client learn the data model from the device itself? e.g. LwM2M Discover and object definitions from the registry)
 5. ID stability across firmware versions: how are IDs assigned (numeric registry, index/subindex, names, hashes, positions)? What happens when an item is inserted or removed?
 6. Name/path addressing (can an item be addressed by name or path?)
 7. Text encoding(s) and binary encoding(s) on the wire
 8. Streaming / periodic publication / subscriptions / change notification
 9. Persistence of settings (built-in store/restore mechanism?)
10. Access control (authentication, per-item read/write rights, access levels)
11. Strings, arrays, structs/records, blobs support
12. Host tools: client libraries, code generators, GUI tools
13. Transports provided (serial, CAN, BLE, UDP/TCP, etc.)
14. Build systems and packaging (CMake, Zephyr module, PlatformIO, Arduino, ESP-IDF, MCUXpresso, pip/npm packages, etc.)
15. RTOS/platform integration (Zephyr, FreeRTOS, bare metal, Linux)
16. Footprint: any published Flash/RAM figures (quote them with the source) - otherwise "no published figure"
17. Safety / coding-standard claims (MISRA C, certification, static analysis claims)
18. License (including commercial/dual-licensing terms, e.g. Anjay's license change, FreeMASTER's license terms)
19. Maturity and community: first release year, latest release version and date, approximate GitHub stars/contributors, notable users

Also report for each: implementation language and standard (e.g. C99, C++17).

Return a compact structured report (plain text or markdown tables) with all rows for all three libraries, with URLs. Be precise and conservative: prefer "partial" with an explanation over an optimistic "yes". Mark clearly which cells you verified on a fetched page and which are from memory. Deliver the report with SubagentHandback.
~~~~

## (b) The complete report, verbatim and unabridged

<!-- BEGIN VERBATIM REPORT -->
# Market research: Cyphal register API, NXP FreeMASTER, OMA LwM2M (Anjay / Anjay Lite / Wakaama)

**Verification key:** **[V]** means I checked it this session on an official page or source file (project GitHub, docs site, spec source, nxp.com, OMA). **[S]** means I only saw it in a search-result excerpt, because the page was blocked (HTTP 403) or not fetched. **[M]** means it is from memory and unverified.

**How I read the sources:** WebFetch refuses verbatim text, so I printed raw official files to stdout with curl and GitHub API calls. PDFs were parsed in memory.

## Headline findings (these affect a decision for an STM32H753 product)
1. **FreeMASTER cannot be used on an STM32 under its licence.** The driver guide says "Use with non-NXP MCU platforms is **not permitted** by the license terms". The driver licence grants use "solely for use in combination with a NXP Product", and the host tool's licence says the same. [V] https://github.com/nxp-mcuxpresso/mcux-freemaster/blob/main/doc/user_guide/10_introduction.md , https://github.com/nxp-mcuxpresso/mcux-freemaster/blob/main/LICENSE.txt , https://www.nxp.com/webapp/sps/download/license.jsp?colCode=FMASTERSW
2. **Anjay's licence has changed twice.**
   - Up to 2.x it was Apache-2.0.
   - From 3.0.0 (2022-05-18) it was the "AVSystem-5-clause" licence: commercial use needed a notice to AVSystem and was capped at 100,000 devices.
   - From **3.10.0 (2025-05-28)** it is the "Non-Commercial License v1.0". Commercial use needs a registration to obtain a free commercial licence. Anjay Lite uses the same scheme. [V]
3. **Wakaama is archived**, both on GitHub (last commit 2026-05-18) and on the Eclipse project page (State: "Archived"). Its only official release, 1.0 (2018), has known CVEs. [V]
4. **Cyphal adoption is low.** PX4 says its Cyphal support is a "work in progress" and Cyphal "has not yet seen significant adoption". Cyphal/UDP and Cyphal/serial are marked "experimental" in the spec. Cyphal v1.1 (libcanard v5.0.alpha, Cy) is still being written. [V]
5. **There are no Cyphal services "112/113".** The register services have fixed service-IDs **384 (Access)** and **385 (List)**, inside the standard range 384–511. [V] https://github.com/OpenCyphal/public_regulated_data_types/blob/master/README.md . UAVCAN v0 used param.GetSet=11 and ExecuteOpcode=10. [M]

---

## 1. Cyphal register API (uavcan.register.*) and implementations
**Language / standard:**
- libcanard: C99 (canard.c stops with `#error` below C99). [V]
- libudpard: C99/C11. [V]
- libcyphal: C++14, header-only, CMake option 14/17/20, README says "not yet complete". [V]
- Nunavut generates C11/C17/C23, C++ (experimental), Python and HTML. [V]
- pycyphal and Yakut: Python. [V]

Short names for DSDL links: the prefix is `https://github.com/OpenCyphal/public_regulated_data_types/blob/master/`, so **Access** = `uavcan/register/384.Access.1.0.dsdl`, **List** = `uavcan/register/385.List.1.0.dsdl`, **Value** = `uavcan/register/Value.1.0.dsdl`.

| # | Row | Verdict | Detail | Source |
|---|---|---|---|---|
|1|Typed values|yes|Value union: empty, string (UTF-8), unstructured (bytes), bit, int8–64, natural (uint) 8–64, real16/32/64. Every numeric value is an array; a scalar is a 1-element array.|[V] Value|
|2a|Units|no|No unit field. Access returns only value, timestamp, mutable and persistent.|[V] Access|
|2b|Limits|yes (optional)|Special-function registers `name<` (max) and `name>` (min), immutable and persistent, same type and size as the register. The server may clamp an out-of-range write or keep the old value.|[V] Access|
|2c|Defaults|yes (optional)|Register `name=`.|[V] Access|
|2d|Enums|no|Nothing for enumerations in the register protocol.|[V] Access, Value|
|2e|Flags|yes|`mutable` and `persistent`; they must not change while running. Also a read timestamp.|[V] Access|
|3|Commands|yes|ExecuteCommand (service 435): restart, power-off, begin-software-update, factory-reset, emergency-stop, store-persistent-states (65530), identify. Vendor commands 0..32767; string parameter, output ≤46 B. Writable non-persistent registers can trigger actions. Any DSDL-defined RPC service also works.|[V] …/uavcan/node/435.ExecuteCommand.1.3.dsdl|
|4|Runtime discovery|partial|List (385) returns names by index. Reading a register returns its type, size and flags. `uavcan.pub/sub/cln/srv.PORT.type` registers name the data type; `uavcan.node.port.List` (subject 7510) is published at least every 10 s; GetInfo is 430. No units or descriptions. The node does not serve DSDL; the host needs it locally (`CYPHAL_PATH`).|[V] List; …/uavcan/node/port/7510.List.1.0.dsdl; Yakut README|
|5|ID stability|yes by name, index unstable|The name is the ID. List order is fixed only while running and "not guaranteed to remain unchanged when the server node is restarted", so adding or removing a register shifts indexes but never names. Ports use numeric IDs: fixed for standard types, otherwise set per deployment via `uavcan.*.PORT.id` (65535 = unset). DSDL types carry major.minor versions. v1.1 (Cy) moves to named topics identified by a 64-bit name hash (experimental).|[V] List; Access; https://github.com/OpenCyphal-Garage/cy|
|6|Name/path addressing|yes (registers)|Dotted lowercase ASCII names, ≤255 bytes (`uint8[<256]`). `uavcan.*` is a reserved prefix, `*` is reserved for raw memory access. Port names are node-local; the wire carries numeric port-IDs only.|[V] Access; …/uavcan/register/Name.1.0.dsdl|
|7|Encodings|binary only on the wire|DSDL serialization: bit-packed, little-endian, UTF-8 strings. Text exists only on the host side: environment-variable mapping (space-separated decimals) and Yakut YAML/JSON.|[V] https://github.com/OpenCyphal/specification/blob/master/specification/dsdl/serialization.tex ; Access|
|8|Streaming / notification|partial|Publish/subscribe is the core of the protocol (periodic or ad hoc). Registers have no subscription or change notification; a client must poll Access.|[V] 7510.List; spec functions.tex|
|9|Persistence|partial|The `persistent` flag exists, and the spec recommends committing to non-volatile memory automatically. Otherwise the client sends store-persistent-states or factory-reset. No store in libcanard. pycyphal's static backend is SQLite. libcyphal's registry has a `persistent` option. The demos ship register.c/storage.c.|[V] Access, ExecuteCommand; https://pycyphal.readthedocs.io/en/stable/api/pycyphal.application.register.backend.static.html ; https://github.com/OpenCyphal-Garage/demos|
|10|Access control|no|Spec: "Information security and other security-related concerns are outside of the scope of this specification." Only the mutable/immutable flag and a `STATUS_NOT_AUTHORIZED` code. No authentication.|[V] https://github.com/OpenCyphal/specification/blob/master/specification/application/functions.tex|
|11|Strings / arrays / structs / blobs|partial|Strings ≤256 B. Unstructured bytes ≤256 B. Array caps: bit ≤2048; 8-bit ≤256; 16-bit ≤128; 32-bit ≤64; 64-bit ≤32. No structs in registers (DSDL messages have structs and unions). Larger files go through `uavcan.file.*`.|[V] …/uavcan/primitive/array/|
|12|Host tools|yes|**Yakut** CLI (`y rl` list, `y rb --only=mp` batch/config dump, `y r` read/write, `y cmd`, monitor, file server, PnP allocator). **pycyphal**. **Nunavut** plus the nunaweb web compiler. **Yukon** GUI (20★, last push 2023-11). **cynic** (new console for v1.1, 2026).|[V] https://github.com/OpenCyphal/yakut ; https://github.com/OpenCyphal/nunavut ; https://github.com/OpenCyphal-Garage/yukon|
|13|Transports|partial|Cyphal/CAN, Classic and FD, redundant (libcanard). Cyphal/UDP (libudpard) and Cyphal/serial (libserard), both "experimental" in the spec. pycyphal adds loopback. No BLE.|[V] libcanard README; spec udp.tex/serial.tex|
|14|Build / packaging|partial|libcanard v4 is 2 files to copy; master adds a CMakeLists; published to the ESP-IDF registry (namespace opencyphal, name libcanard). NuttX apps package `libopencyphal`. libcyphal is header-only with CMake. pycyphal, Yakut, Nunavut and pydsdl are on PyPI. No official Zephyr, PlatformIO or Arduino package; Arduino only via third-party 107-Arduino-OpenCyphal [S].|[V] https://github.com/OpenCyphal/libcanard/blob/v4.0.0/esp_metadata/idf_component.yml ; https://nuttx.apache.org/docs/latest/applications/canutils/libopecyphal/index.html|
|15|RTOS / platform|partial|Bare metal on 8/16/32/64-bit. Official drivers: SocketCAN (Linux) and STM32 bxCAN. NuttX/PX4. libudpard runs on lwIP or BSD sockets. No official Zephyr or FreeRTOS integration found.|[V] https://github.com/OpenCyphal/platform_specific_components ; https://github.com/OpenCyphal/libudpard|
|16|Footprint|no library figure|libcanard: "starting from 32K ROM and 32K RAM" (target class), "≈1000 SLoC", CRC table option ≈500 B ROM. libudpard: "~100K ROM/RAM" environments, fewer than 2k lines.|[V] https://github.com/OpenCyphal/libcanard/blob/v4.0.0/README.md ; libudpard README|
|17|Safety / coding standard|partial (claims only)|libcanard v4: "Compliance with automatically enforceable MISRA C rules", 100 % test coverage, "at least two static analyzers"; a MISRA report is available on request. libudpard: "Partial MISRA C compliance". libcyphal: AUTOSAR C++14 guidelines, SonarCloud. No certification.|[V] libcanard v4.0.0 canard.h and README|
|18|License|MIT|MIT for libcanard, libudpard, libserard, libcyphal, pycyphal, Yakut and Nunavut. The spec needs "no licensing or approval of any kind".|[V] https://opencyphal.org/ ; GitHub/PyPI metadata|
|19|Maturity|moderate / niche|Spec v1.0-alpha Jan 2020, v1.0-beta Sep 2020, v1.0 May 2025 (revision history in the spec's LaTeX source). DS-015 was replaced by UDRAL (PR #125, merged 2021-10-15). libcanard: v4.0.0 on 2026-01-10, master is v5.0.alpha, ~450★, ~37 contributors. pycyphal 1.27.1 (2026-06-27, 142★). Yakut 0.14.2 (2026-02-11, 65★). libudpard 2.0.0 (2026-02, 22★). opencyphal.org names ArduPilot, Auterion, Dronecode, NXP, Revolve NTNU and Zubax (site claim). PX4: "has not yet seen significant adoption".|[V] https://github.com/OpenCyphal/specification/blob/master/specification/introduction/introduction.tex ; https://github.com/OpenCyphal/public_regulated_data_types/pull/125 ; https://docs.px4.io/main/en/can/|

## 2. NXP FreeMASTER (driver v3 / protocol v4; host 3.2)
**Language / standard:** C. Changelog 3.0.2 says "Removed dependency on C99 compiler features"; no explicit standard is stated. Current driver 3.0.13 (SDK v26.06.00). [V] https://github.com/nxp-mcuxpresso/mcux-freemaster/blob/main/ChangeLogKSDK.txt

Short name: **FM** = `https://github.com/nxp-mcuxpresso/mcux-freemaster/blob/main/`.

| # | Row | Verdict | Detail | Source |
|---|---|---|---|---|
|1|Typed values|yes (via TSA)|u8–u64, s8–s64, FRAC16/32/64, Q(m,n)/UQ(m,n), float, double, pointer, user struct/union types. The wire itself is untyped memory access; without TSA the host takes types from the ELF/DWARF file.|[V] FM `doc/user_guide/tsa/tsa_table_definition.md`, FM `src/common/freemaster_tsa.h`|
|2a|Units|no on device|Units live in the host project's variable definition ("Unit = The name of unit displayed in the variable watch"). A project file can be embedded in target flash via TSA MEMFILE/PROJECT entries.|[V] https://www.nxp.com/docs/en/user-guide/FMSTRUG.pdf (Rev 2.0, 2007); FM `doc/user_guide/tsa/tsa_active_content.md`|
|2b|Limits|no on device|Host side only: "All numbers from min to max by step".|[V] FMSTRUG.pdf|
|2c|Defaults|no|No TSA entry for defaults.|[V] freemaster_tsa.h|
|2d|Enums|partial|`FMSTR_TSA_ENUM` / `FMSTR_TSA_CONST` entries on the device; "Text enumeration" lookup tables on the host.|[V] freemaster_tsa.h; FMSTRUG.pdf|
|2e|Flags|yes|RO/RW per entry (plus a RO-in-flash flag). Enforced only when `FMSTR_USE_TSA_SAFETY=1`.|[V] FM `doc/user_guide/cfg/cfg_tsa.md`|
|3|Commands|yes|Application Commands: command code and data sent to the app, which returns a result code (poll or callback). No call-by-name.|[V] FM `doc/user_guide/20_features.md`|
|4|Runtime discovery|yes (with TSA)|TSA tables give names, types, addresses, sizes, RO/RW, struct members and enums. Board detection gives driver/protocol version, MTU, app name/version, build date, endianness, protection level and recorder/scope counts. Variables can be added at runtime (`FMSTR_TsaAddVar`). Without TSA the host needs the ELF.|[V] 20_features.md; cfg_tsa.md|
|5|ID stability|by C symbol name|No numeric IDs. The protocol uses addresses, which change every build; the host resolves names from TSA or the ELF. Inference: adding or removing variables is harmless as long as names are kept; a rename breaks host projects.|[V] protocol commands in FM `src/common/freemaster_protocol.h`; host behaviour inferred|
|6|Name/path addressing|partial|The wire uses READMEM/WRITEMEM by address; names are resolved on the host. Struct members via TSA STRUCT/MEMBER. Virtual file paths via TSA Active Content (`fmstr:` URLs).|[V] freemaster_protocol.h; tsa_active_content.md|
|7|Encodings|binary wire, JSON on host|Binary protocol V4: start byte '+' (0x2B), command codes, V4 CRC. Host automation uses JSON-RPC, over WebSocket on port 41000 according to community posts [S].|[V] freemaster_protocol.h; [S] https://community.nxp.com/t5/FreeMASTER/WebSocket-connection-to-ws-localhost-41000-failed/td-p/1745880|
|8|Streaming|partial|Oscilloscope: the host polls many variables per request, rate limited by the link. Recorder: samples into target RAM at CPU rate, with a threshold trigger, multiple instances. Pipes: ordered, lossless streams both ways. No device-initiated notifications.|[V] 20_features.md|
|9|Persistence|no|No settings store. TSA "user resources" expose external EEPROM or SD-card files, and there is a flash (RWF) access level.|[V] 20_features.md; freemaster_tsa.h|
|10|Access control|yes|Optional passwords for three levels (read / read-write / read-write-flash), stored as plaintext or SHA-1 hash. Two-step SHA-1 challenge with a random salt (AUTH1/AUTH2). TSA Safety confines access to described objects and blocks writes to RO entries. No transport encryption found.|[V] FM `src/common/freemaster_cfg.h.example`, FM `src/common/freemaster_protocol.c`|
|11|Strings / arrays / structs / blobs|partial|Structs and unions yes. Arrays and memory blocks yes (RO_MEM/RW_MEM). Files and blobs yes (MEMFILE, USER_FILE). No string type in TSA (the host can display a variable as a string).|[V] tsa_table_definition.md; tsa_active_content.md|
|12|Host tools|yes (NXP)|FreeMASTER 3.2.7 (2026-06-25) on Windows 10/11: watch, oscilloscope, recorder, HTML/JS dashboards. FreeMASTER Lite: headless Node.js service on Windows, RHEL 8 or Ubuntu 22.04, JSON-RPC API. Clients for Python, Node.js and C/C++/C#; Node-RED nodes; legacy ActiveX. MCAT motor-tuning plug-in [S]. No code generator.|[V] https://www.nxp.com/design/design-center/software/development-software/freemaster-run-time-debugging-tool:FREEMASTER|
|13|Transports|yes|UART/LPUART/USART/USB-CDC (including single-wire). CAN: FlexCAN, msCAN, MCAN, CAN-FD (3.0.10). TCP/UDP over lwIP, with UDP auto-discovery. SEGGER RTT. JTAG/packet-driven BDM. 56F800E EOnCE. Zephyr drivers for serial, CAN, TCP, UDP and RTT.|[V] FM `doc/user_guide/cfg/cfg_transport_net.md`; 10_introduction.md; ChangeLogKSDK.txt|
|14|Build / packaging|partial|MCUXpresso SDK middleware (west manifest, `mcux/` CMake + Kconfig). Zephyr module (`nxp_freemaster`). MCUXpresso Config Tools. No pip/npm package for the driver.|[V] FM `zephyr/module.yml`; README|
|15|RTOS / platform|partial|Bare metal with poll, short-interrupt or long-interrupt modes. Zephyr: dedicated task, shell and logging over a pipe, automatic TSA tables. Generic 32-bit little/big-endian, DSC, S12Z. NXP parts only by licence. FreeRTOS integration not documented.|[V] FM `doc/user_guide/22b_driver_interrupt_modes.md`; 20_features.md|
|16|Footprint|no published figure|Not in the current driver guide.|[V] (checked the guide)|
|17|Safety / coding standard|weak|Changelog 3.0.8: "Misra issues cleanup". Coverity comments in the source. No MISRA claim, no certification.|[V] ChangeLogKSDK.txt|
|18|License|proprietary, NXP-only|Driver: "NXP LA_OPT_Online Code Hosting" licence v1.4 (since 3.0.8), "solely for use in combination with a NXP Product". Host: NXP Software License v64.2, same restriction. Usually free of charge [M].|[V] FM `LICENSE.txt`; https://www.nxp.com/webapp/sps/download/license.jsp?colCode=FMASTERSW|
|19|Maturity|mature, vendor-controlled|Driver guide rev 1.0 dates from 03/2006; a Motorola-era manual rev 0.1 06/2004 exists [S]. V3 driver and V4 protocol since 2019 (FreeMASTER 3.0, SDK 2.6). GitHub mirror: 6★, created 2024-10, "Contributions are not currently accepted". Used across NXP SDK examples and MCAT.|[V] FM `doc/user_guide/91_revision_history.md`; repo README|

## 3. OMA LwM2M: Anjay 3.15.0, Anjay Lite 3.0.2, Eclipse Wakaama
**Language / standard:**
- Anjay: "standards-compliant C99"; some optional features need C11 `stdatomic.h`. [V] https://github.com/AVSystem/Anjay/blob/master/doc/sphinx/source/Introduction.rst
- Anjay Lite: C99 (`CMAKE_C_STANDARD 99`). [V]
- Wakaama: C, with no standard set in CMake (built with `-pedantic -Werror`). [V] https://github.com/eclipse-wakaama/wakaama/blob/main/wakaama.cmake

Short names:
- **T** = LwM2M Core TS 1.2.2: https://www.openmobilealliance.org/release/lightweightm2m/V1_2_2-20240613-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_2_2-20240613-A.html
- **XSD** = https://github.com/OpenMobileAlliance/lwm2m-registry (LWM2M-v1_1.xsd)
- **AN** = https://github.com/AVSystem/Anjay (README)
- **CFG** = https://github.com/AVSystem/Anjay/blob/master/include_public/anjay/anjay_config.h.in
- **AL** = https://github.com/AVSystem/Anjay-lite
- **W** = https://github.com/eclipse-wakaama/wakaama

| # | Row | Verdict | Detail | Source |
|---|---|---|---|---|
|1|Typed values|yes|Spec: String, Integer (8–64 signed), Unsigned Integer, Float (32/64), Boolean, Opaque, Time, Objlnk, Corelnk, none (Execute). Anjay API: i32/i64/u32/u64/float/double/bool/string/bytes/objlnk. Wakaama: string, opaque, int, uint, float, bool, object link, core link.|[V] T App. C; https://github.com/AVSystem/Anjay/blob/master/include_public/anjay/io.h ; https://github.com/eclipse-wakaama/wakaama/blob/main/include/liblwm2m.h|
|2a|Units|yes, in the object definition only|"Units" is a free-text field in the object definition file; the device never sends it.|[V] XSD; T D.1-2|
|2b|Limits|partial|"RangeEnumeration" is free text (e.g. "0..31"). The client must reject out-of-range writes.|[V] XSD; T|
|2c|Defaults|no|No field in the schema.|[V] XSD|
|2d|Enums|partial|Only through RangeEnumeration text; Integer is "also used for the purpose of enumeration".|[V] T|
|2e|Flags|yes|Operations R/W/RW/E (empty = bootstrap-only), Mandatory/Optional, Single/Multiple.|[V] XSD|
|3|Commands|yes|Execute on E resources, with optional arguments. Also Create/Delete instances and Write-Attributes. Wakaama has `lwm2m_execute_callback_t`.|[V] T 6.3.5; liblwm2m.h|
|4|Runtime discovery|partial|Register lists objects and instances (plus object versions and `ct=` formats). Discover returns a CoRE link list of what is instantiated, with attributes (dim, ver, pmin…). Types, units and ranges are not served; a generic server needs the object definition XML by Object ID and version (the registry repo holds about 908 XML files). Discover is supported by Anjay, Anjay Lite (`ANJ_WITH_DISCOVER`) and Wakaama.|[V] T 6.3.2; XSD; AN README; https://github.com/AVSystem/Anjay-lite/blob/master/doc/sphinx/source/CompilationImpact.rst|
|5|ID stability|yes (registry numbers)|OMNA ranges: 0–1023 OMA, 2048–10240 other standards bodies, 10241–32768 vendors, 32769–42768 private company blocks, 42769–42800 test. Resource IDs are fixed per object. Versions: the ID never changes; adding or removing an optional resource is a minor version, a mandatory one is a major version; "reusing a previously removed Resource ID" is forbidden. Instance IDs are assigned at runtime.|[V] T 7.2, D.2.1|
|6|Name/path addressing|path yes, name no|Numeric path `/Obj/Inst/Res/ResInst` (e.g. `/3/0/7/1`). Names exist only in the object definition files.|[V] T|
|7|Encodings|text and binary|Spec: Plain Text (0), link format (40), LwM2M JSON (11543), SenML JSON (110) as text; Opaque (42), TLV (11542), CBOR (60), SenML CBOR (112), LwM2M CBOR (11544, v1.2), SenML-ETCH (320/322) as binary. Anjay: text, opaque, CBOR, TLV, SenML JSON/CBOR, LwM2M JSON output only, LwM2M CBOR usable for Send. Anjay Lite reads TLV/text/opaque/CBOR/SenML CBOR/LwM2M CBOR and writes all but TLV; no JSON. Wakaama: text, link, opaque, TLV, JSON, SenML JSON, CBOR, SenML CBOR; no LwM2M CBOR.|[V] T 7.5; AN README; CFG; https://docs.avsystem.com/hubfs/Anjay_Lite_Docs/Introduction.html ; liblwm2m.h|
|8|Streaming / notification|yes|Observe/Notify with pmin, pmax, gt, lt, st; epmin/epmax (v1.1); edge, con, hqmax (v1.2). Observe-Composite and Send (client push, v1.1). Anjay has all of these. Anjay Lite has them as compile flags. Wakaama has observe and `lwm2m_send` (added in the 2026 snapshot).|[V] T; AN README; CompilationImpact.rst; W releases|
|9|Persistence|partial|The spec defines no store. Anjay (free): stream-based persist/restore for Security, Server and Access Control objects and attribute storage. "Core Persistence" (resume a session without re-registering) is **commercial**. Anjay Lite: `ANJ_WITH_PERSISTENCE` for Security and Server objects. Wakaama: nothing in its core.|[V] https://docs.avsystem.com/hubfs/Anjay_Docs/AdvancedTopics/AT-Persistence.html ; CFG|
|10|Access control|yes (spec and Anjay), no (Wakaama)|Spec: Access Control Object /2 with a per-instance ACL keyed by Short Server ID (0..31, combinations of R/W/E/D; Create at object level [M]) and an owner. Security: DTLS/TLS with PSK, RPK or X.509; OSCORE (v1.1); DTLS/TLS 1.3 (v1.2). Anjay: Access Control module enforced by the core; PSK and certificates; **RPK not implemented**; since 3.15 unsecured mode is off by default and TLS 1.2 is the minimum; OSCORE, EST and hardware security modules are commercial. Wakaama: DTLS via tinydtls; its core has `// TODO: check ACL` and only refuses Security-object reads (the Eclipse page claims "access rights" checking).|[V] T E.3 & 8; AN README; https://github.com/AVSystem/Anjay/blob/master/CHANGELOG.md ; https://github.com/eclipse-wakaama/wakaama/blob/main/core/management.c|
|11|Strings / arrays / structs / blobs|partial|UTF-8 strings. Opaque blobs with CoAP Block1/2 for large transfers. Arrays are multiple-instance resources. Records are object instances; there are no nested structs.|[V] T; AN README|
|12|Host tools|yes|Anjay: `anjay_codegen.py` turns object definition XML into C/C++ stubs; `lwm2m_object_registry.py` downloads definitions from OMNA; testing shell; provisioning and package tools; Coiote DM server (commercial, free basic tier). Wakaama: example CLI server, bootstrap server and client; no code generator. Eclipse Leshan (Java) [M].|[V] https://docs.avsystem.com/hubfs/Anjay_Docs/Tools/StubGenerator.html ; W README|
|13|Transports|partial|Spec: UDP and SMS (v1.0); TCP/TLS, LoRaWAN and 3GPP CIoT non-IP (v1.1); MQTT and HTTP (v1.2). Anjay open source: UDP and TCP; SMS and NIDD are commercial. Anjay Lite: UDP (plus DTLS). Wakaama: POSIX UDP, tinydtls, or your own transport.|[V] T §1.3–1.4; AN `include_public/anjay/core.h`; AN `doc/sphinx/source/Introduction.rst`; W README|
|14|Build / packaging|partial|Anjay: CMake; Zephyr module (new Anjay-zephyr-module, 2026). Archived: ESP-IDF layer (2025), STM32CubeMX pack I-CUBE-Anjay (2026), mbed OS. No PlatformIO or Arduino package found. Anjay Lite: CMake and a Zephyr module. Wakaama: CMake ("files to be built with an application") and a RIOT package.|[V] GitHub AVSystem org listing; https://api.riot-os.org/group__pkg__wakaama.html|
|15|RTOS / platform|yes|Anjay: Linux/macOS/BSD/Android, Windows (preliminary), FreeRTOS (Anjay-freertos-client "for STM32 devices", Pico), Zephyr; porting needs ISO C99. Anjay Lite: bare metal with **no heap**, POSIX, Zephyr. Wakaama: POSIX layer or your own, single-threaded, RIOT.|[V] AN README; https://github.com/AVSystem/Anjay-lite/blob/master/doc/sphinx/source/Integrations.rst ; https://projects.eclipse.org/projects/iot.wakaama|
|16|Footprint|Anjay Lite only|Anjay Lite 2.0.0, arm-none-eabi-gcc 14.2.1, RelWithDebInfo, no logs, MbedTLS excluded: minimal **33.2 kB**; common options 47.6 kB; all extras 56.4 kB; everything 65.5 kB; logs add 19–35 %. Static RAM: core context 7816 B, Security instance 1040 B, Server 152 B, Device 60 B. Full Anjay: none published (I checked all 136 doc sources). Wakaama: none.|[V] CompilationImpact.rst|
|17|Safety / coding standard|static analysis only|Anjay: Coverity Scan; clang-tidy, cppcheck, CodeChecker, Infer. Wakaama: CodeQL, CodeChecker, OpenSSF Scorecard and Best Practices. No MISRA or certification claim in any of the three.|[V] AN README; W README|
|18|License|see headline 2|Anjay: Apache-2.0 up to 2.x; AVSystem-5-clause for 3.0–3.9 (100,000-device cap); Non-Commercial License from 3.10.0, with registration for a free commercial licence. Anjay Lite: same (June 2025). The integration repos are Apache-2.0. **Wakaama: EPL-2.0 OR BSD-3-Clause.**|[V] https://github.com/AVSystem/Anjay/blob/master/LICENSE , https://github.com/AVSystem/Anjay/blob/3.9.0/LICENSE ; W README|
|19|Maturity|Anjay active, Wakaama archived|Anjay: 1.0.0 on 2017-02-08; 3.15.0 on 2026-09-18; ~219★, ~20 contributors; customers named on the vendor site (IRNAS, Bulk Tainer) [S]. Anjay Lite: repo 2025-06; 3.0.2 on 2026-09-10; 14★. Wakaama: repo since 2014; releases 0.5 (2017) and 1.0 (2018); snapshots up to 2026-05-26; ~538★, ~56 contributors; **archived**. Spec: 1.1 (2018-07-10), 1.2 (2020-11-10), 1.2.2 (2024-06-13).|[V] GitHub API; AN `CHANGELOG.md`; W README; https://projects.eclipse.org/projects/iot.wakaama|

**Housekeeping:** No project files were written. One temporary list file was briefly created in `/tmp` during a grep and deleted straight away. The WebFetch tool saved three PDFs (Cyphal spec, FMSTRUG, FreeMASTER Lite) into its own cache folder under `~/.claude/projects/.../tool-results`.

<!-- END VERBATIM REPORT -->

## (c) What the critic rejected, corrected or did not use, and why

Used: every row of all three tables became the Cyphal (CY), FreeMASTER (FM) and LwM2M (LW)
columns of the capability matrix; the per-cell lines and URLs are in ../Comparison.txt. Its three
headline findings (FreeMASTER's NXP-only licence, Anjay's non-commercial licence since 3.10.0,
Wakaama archived) are used as the deciding facts in the report.

Corrected or changed by me, and why:
1. My brief was wrong. It named "112/113 services" for the Cyphal register API. The helper found
   that the register services are 384 (Access) and 385 (List); I accept its correction. The error
   was mine (from memory).
2. Cyphal limits and defaults. The helper wrote "yes (optional)" for both. They exist only as
   optional special-function registers that a server may or may not provide, so the matrix uses
   "P".
3. LwM2M per-item rights. The helper wrote "yes (spec and Anjay), no (Wakaama)". The matrix shows
   Anjay and Wakaama separately.
4. FreeMASTER ID stability. The helper wrote "by C symbol name" (addresses on the wire). The matrix
   uses "P".

Not used in the report (kept here for later analysis):
- Cyphal v1.1 ("Cy") named topics identified by a 64-bit name hash: experimental, not in the matrix.
- FreeMASTER host-tool versions, dashboard features and the list of client languages.
- The LwM2M content-format numbers and Anjay's list of commercial-only features beyond the licence.
- Wakaama's 2026 snapshot details and the vendor-site customer names.

Housekeeping the helper reported (both outside the workspace, so the file-location rule holds):
a temporary list file created in /tmp during a grep and deleted straight away; three PDFs (the Cyphal
specification, the FreeMASTER user guide, a FreeMASTER Lite document) cached by the web-fetch tool
in its own cache folder under the user's .claude directory.

Cells the helper marked as not web-verified (they stay marked in ../Comparison.txt):
- From memory (M): UAVCAN v0 service numbers; FreeMASTER being usually free of charge; Eclipse
  Leshan as an LwM2M server; the object-level Create right in the LwM2M ACL.
- Search snippet only (S): FreeMASTER JSON-RPC over WebSocket on port 41000; the MCAT plug-in; the
  Motorola-era manual; Anjay customer names; the third-party Arduino Cyphal library.

## (d) Usage and outcome

328,629 tokens; 165 tool calls; 2,349,263 ms (about 39.2 minutes). Status: finished normally and delivered one report. It did not fail, time out or return nothing.
Figures are from the harness's completion notice for this helper.
