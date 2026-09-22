# Indexed metadata

Baseline: `f1cfbd891ef2f0c23f0fb2dae70be8ed4bd6fcaf`, binary resource v2.
ABI 8 changes metadata pointer semantics, while ARM32 FieldType/Field/Command
remain 48/96/20 bytes. Frozen ABI 6 and 7 callers must fail to link against the
current core and JSON archives. Resource constructor boundaries also reject ABI 7.

`TelemetryMetadataCheck.cpp` covers indexed versus sequential order, automatic
and explicitly ordered enums, U64/S64 extrema, names containing NUL, absent versus
empty labels, zero-argument versus reserved commands, invalid indices, null sinks,
early stop, nested visitors and noncopyable const/volatile visitors. The ordinary
host, sanitizer and ARM runners include it.

`Sequential.cpp` is deliberately compatible with both baseline and current
headers. Compile it against each tree with Cortex-M7 hard-float GCC 14.3.1 at O2
and Os. The enum fold has the same instruction stream after replacing its own
symbol name; it does not loop through `at()`. Command traversal retains the
original parameter-pack fold. Constant descriptors can inline the ops access.
An unknown descriptor needs **one additional load of the sequential callback**
at the traversal boundary. That is a fixed ops indirection, not a check/dispatch
for every parameter. It is not honest to describe all runtime metadata wrappers
as instruction-identical. Field read/write and native command calls do not read
these ops; the existing direct/local/global ARM checks remain the hot-path gate.

## Enum storage experiment

Run `enum_sizes.py --cxx <arm-none-eabi-g++> --size <arm-none-eabi-size>
--build-dir <output>`. It generates three implementations with the same sequential
fold and compares complete `.text + .rodata + .data`, including the ops object,
names, indexed table and emitters. It does not measure board cycles.

GCC 14.3.1 Cortex-M7 results in bytes; each cell is function table / switch / data table:

| Entries | Raw bits | O2 | Os |
| --- | --- | --- | --- |
| 3 | 16 | 361 / 333 / 273 | 287 / 271 / 265 |
| 3 | 64 | 377 / 333 / 277 | 275 / 259 / 277 |
| 16 | 16 | 1742 / 1266 / 1082 | 1372 / 1298 / 1114 |
| 16 | 64 | 1822 / 1406 / 1098 | 1308 / 1234 / 1102 |
| 64 | 16 | 6934 / 4746 / 4162 | 5488 / 5178 / 4390 |
| 64 | 64 | 7206 / 5478 / 4178 | 5232 / 4922 / 4182 |

The native `{code, string_view}` table wins 11/12 fixtures and removes the extra
indexed emitter call. It is the selected general implementation. A three-entry
U64 switch is 18 bytes smaller at Os in this fixture; no universal size or speed
claim follows from these results. Sequential folds stay independent of the
indexed storage choice. No resource cursor or binary payload changes belong to
this first stage.
