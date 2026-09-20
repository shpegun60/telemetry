# Serializer traversal through the public API

Baseline: `a2339b233d7e95dbe01cbf3e70c2068d3d07b689`, already ABI 7 and
[7/7 CI checks passed](https://github.com/shpegun60/telemetry/actions/runs/35527729254).
The refactor at `492805c` changes neither ABI, schema format nor fingerprint
semantics. The measurements below record that checkpoint, before the subsequent
[versioned metadata addition](SCHEMA_META.md).

`TelemetryJson.cpp` and `TelemetryCommandJson.cpp` use `catalogs()` and indexed
field/command entries for schema output and hashing. Values and local command
descriptors use raw range iteration; values need no ID calculation. Parameters
flow through `forEachParameter()`. The serializers no longer assemble packed
IDs or maintain their own nested pointer/count loops.

The first conversion exposed two unnecessary costs in the new convenience API:
rechecking already-capped ranges and passing a pointer to a pointer to a visitor.
The final version uses a private range factory only from already-normalized
catalog/index owners. Public pointer/count ranges still validate their bounds.
Parameter callbacks borrow the visitor directly with its exact cv-qualified
type. A function reference goes through a local function-pointer object, so
function addresses are never cast to object addresses.

Internal parameter visitors own the hash word or the existing JsonWriter
state; static assertions require no storage growth. They share the same schema
logic for local and grouped commands. No dynamic parameter array is introduced.

## Behavior checks

The schema comparison compiles identical field/command declarations against
the archived baseline and the updated source. Complete field schema (including
fingerprint), values and grouped command schema are byte-identical:

```sh
python tests/audit/compare_flags_schema.py --baseline /path/to/a2339b2 --current /path/to/492805c --cxx g++ --output /path/to/serializer-parity --unchanged
```

Existing JSON and command suites additionally sweep every buffer size, both
64-bit JSON modes, empty/invalid descriptors, enum dictionaries and escaping.
They check that an insufficient prefix prevents getter calls, a failed value
stops further reads, and a successful value frame reads each getter once.

Traversal coverage now contains **88 runtime checks** and **25 rejected programs**.
New cases exercise direct function references, const function pointers,
const/volatile non-owning visitors, external range clipping and the private
range-construction boundary. The host runner also checks both invalid runtime
Persistent constructors. Existing native read/write/call probes remain intact.
MSVC 14.50 accepted an outside call to the private range factory when its last
argument had a default; requiring that argument explicitly restores the expected
access diagnostic. The retained negative case covers the final signature.

## ARM comparison

CubeIDE GCC 14.3.1, Cortex-M7, Thumb, `fpv5-d16` hard float, C++17,
no exceptions/RTTI, `-O2` and `-Os`. These are summed `.text*` object sections,
including COMDAT functions; they are not linked image sizes:

| Object | Before O2 | After O2 | Before Os | After Os |
| --- | ---: | ---: | ---: | ---: |
| Field JSON | 10348 | 10420 | 6546 | 6634 |
| Command JSON | 8092 | 8132 | 4258 | 4258 |

Read-only data sizes are unchanged, with no `.data` or `.bss`. The serializer
instructions are **not identical**: iterator routing changes register allocation
and the object totals grow by 112 bytes at O2 and 88 bytes at Os. This is a
source simplification, not a measured serializer speedup.

Selected `-fstack-usage` frames, before -> after:

| Function | O2 | Os |
| --- | --- | --- |
| Field values | 104 -> 104 | 88 -> 80 |
| Field schema | 136 -> 136 | 128 -> 128 |
| Local command schema | 80 -> 88 | 56 -> 48 |
| Grouped command schema | 88 -> 88 | 80 -> 80 |
| Grouped command fingerprint | 56 -> 56 | 40 -> 56 |
| Command fingerprint helper | 24 -> 24 | 24 -> 24 |

These are individual compiler-reported frames, not a cumulative call-chain or
board stack bound. No board timing was measured. Descriptor sizes and native
Field/Command dispatch contracts are unchanged.

To inspect the objects in both source trees, compile each serializer with:

```sh
arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti -O2 -fstack-usage -Ilib/telemetry -c lib/telemetry/serialization/TelemetryJson.cpp -o /path/to/TelemetryJson.o
arm-none-eabi-size -A /path/to/TelemetryJson.o
arm-none-eabi-objdump -drC /path/to/TelemetryJson.o
```

Repeat for `TelemetryCommandJson.cpp` and `-Os`. Full checks use
`tests/run_checks.py` and `tests/run_arm_checks.py`; their output directories
retain individual compiler, linker, header and rejection logs.
