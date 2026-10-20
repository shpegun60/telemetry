# Runtime schema qualification

Run from the repository root with an external output directory:

```sh
python3 -B tests/runtime_schema/run.py --cxx g++ --build-dir /external/runtime-gcc
python3 -B tests/runtime_schema/run.py --cxx clang++-18 --sanitize --build-dir /external/runtime-sanitized
```

The Native producer exports a real model descriptor and compares core
`dispatchEncoded(FieldRead)` bytes with the static Codec. A consumer including
only runtime headers checks scalar bit patterns, nested arrays/structs, open
enums, empty values, prepared paths, workspace identity/invalidation, exact
capacities, overlap and malformed values. A generated standalone SDK decodes
and re-encodes both native payloads byte for byte. Compiler dependency output
rejects reflection, Resource, Client and Integration in the runtime-only probe.

The same runtime checks execute with all replaceable C++ allocation functions
trapped. A separate control must exit 86 through the allocation trap. This is a
C++ allocation proof for the tested paths; the file-loading harness uses C stdio.

The lifetime checks exercise temporary parse results, copy/move operations,
foreign workspaces and failed reparsing. They actually free descriptor, payload
and index storage after invalidation while keeping the workspace object alive;
the sanitized suite checks that stale access never touches those buffers.

On Linux, a separate plain executable protects a 64 KiB Boolean payload with
`mprotect`. Navigation succeeds with all payload pages inaccessible; prepared
scalar reads succeed with only the selected page accessible. Two child-process
controls must receive `SIGSEGV` from a direct protected read and whole-value
validation. This proves the pages are protected and detects accidental subtree
rescans without timing thresholds. The controls run without sanitizer signal
interception, including in the sanitized suite. Other hosts report this probe
as skipped; the runtime reader itself has no operating-system dependency.

The parser corpus includes rehashed semantic mutations, section truncations,
depth boundaries, UTF-8 names, and seeded random inputs. Every acceptance result
is compared with the existing independent Python descriptor parser. The runner
retains command logs, binary/generated/fixture hashes, corpus classifications and
before/end source hashes in `summary.json`. These are host checks, with no board
execution or transport behavior claimed.
