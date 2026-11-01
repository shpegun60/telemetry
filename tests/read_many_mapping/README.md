# ReadMany mapping qualification

This suite qualifies the [host prototype](../../examples/read_many_mapping/README.md).
It does not implement or test production ReadMany scheduling in DeviceClient.

Host commands are in the example README. Both GCC and Clang ASan/UBSan profiles
install selected modules and use their actual headers/archives. They retain
source/tool/input hashes, build commands, logs, JSON, binaries and `.su` records.
The existing [read comparison harness](../read_comparison/run.py) supplies the
shared installation/evidence procedure; its original entry point remains usable.

Controls exercise count 0/1/32/33, duplicates, unknown/unsupported/unavailable
items, cross-catalog IDs, response budgets, input/output/Workspace overlap,
remaining scratch with a live lease, invalid capabilities, session/schema changes,
malformed/truncated/reordered replies and canonical bool bytes. A coherent erased
Field thunk deliberately writes bytes before failing; its payload is suppressed
and the following item remains valid. U64/S64, open signed enums, NaN payload bits
and signed zero are checked without floating-point equality.

The ordinary Service has fixed request/reply wire sizes of 130/514 bytes. Invalid
batch contents can enter the Service callback, but invoke zero Field getters.
Insufficient outer response capacity prevents even that Service callback.
Unused Service slots and error payload bytes remain zero. Both mappings use one
shared executor.

No-heap checks arm replacement C++ new/delete and GNU allocation-call wrappers
around real prototype work. Separate `new`, `aligned_new`, `malloc`, `calloc`,
`realloc` and `free` controls prove the traps are active (expected exit 86).
This qualifies the linked code paths, not all internal allocations of the OS or
sanitizer runtime. Sanitized host runs retain leak checking enabled.

## Offline ARM

```sh
python3 -B tests/read_many_mapping/run.py --arm-only --cxx arm-none-eabi-g++ \
  --cmake cmake --ninja ninja --build-dir /tmp/telemetry-read-many-arm
```

This installs separate Cortex-M7 packages under `-Os` and `-O2`. Each optimization
links two separate GC-selected images, one compact entry and one ordinary Service
entry. The runner verifies installed include/archive paths, required entry retention,
absence of the opposite entry and absence of allocation symbols. It retains
`size -A`, `nm -S -C`, `objdump -drwC`, maps, binaries and compiler `.su` records.

Linked sections include the selected fixture/application code. They are not a
complete firmware Flash/RAM budget. A `.su` entry is one compiler stack frame,
not a worst-case call-chain stack bound. No board is programmed or executed.

CI runs the host profiles after its pinned COBS checkout and the ARM mode in the
existing Cortex-M7 job. Evidence is retained in `generated-qml-sdk` and
`cortex-m7-check-logs` respectively. Source equality is checked before/after each
run; use a new output directory for every invocation.
