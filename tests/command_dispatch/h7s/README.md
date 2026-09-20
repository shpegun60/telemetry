# Native command dispatch DWT fixture

This fixture measures the three `CommandTable` execution levels on a 600 MHz
NUCLEO-H7S3L8 with instruction and data caches enabled:

1. `CommandTable::call<1>(float, Mode)`;
2. `CommandTable::call(runtimeIndex, float, Mode)`;
3. `CommandIndex::execute(id, Scalar*, count)` with two Scalars prepared before
   the timed interval.

Each path enters an identical `noinline` wrapper and the same `noinline` owner
method. Runtime `float` and enum values prevent compile-time value folding. The
fixture disables interrupts, warms the selected path, executes 65,536 calls and
records nine DWT windows per path at both `-O2` and `-Os`. A checksum proves that
every invocation reached the owner. The runner first backs up the target's first
64 KiB of Flash and restores and reads it back in a `finally` block.

The build uses the isolated Cube scaffold prepared for the field-layout H7S
checks. From the repository root:

```powershell
python tests/command_dispatch/h7s/run.py `
  --output build/command-dispatch-live
```

`--cube`, `--arm-cxx`, `--programmer`, `--serial` and `--port` override the
local defaults. The runner refuses a modified tracked tree, so every retained
measurement names one exact source commit. It writes `receipt.json` and
`samples.csv` under the selected output directory. After copying an accepted
pair beside this file, verify it without a board:

```powershell
python tests/command_dispatch/h7s/verify.py --self-test
```

The offline verifier checks all 54 timing windows, image/object/library hashes,
checksums, the recomputed summary and exact before/after firmware hashes. Its
mutation controls prove that missing, duplicated or altered evidence is rejected.
