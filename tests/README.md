# Telemetry checks

Run from the repository root with Python 3 and a GCC-compatible C++ compiler.
The checks have no Qt dependency. Generated binaries and logs go only to the
directory specified by `--build-dir`:

```sh
python3 tests/run_checks.py --cxx g++ --std c++17 --build-dir build/checks-gcc17
python3 tests/run_checks.py --cxx clang++ --std c++20 --build-dir build/checks-clang20
python3 tests/run_checks.py --cxx clang++ --std c++17 --sanitize --build-dir build/checks-sanitized
```

On Windows, use `python` and the installed Qt MinGW `g++.exe`. Include that
compiler's `bin` directory in PATH for its runtime DLLs. The corresponding
`telemetry_*_check.pro` files also build each suite through the library's `.pri`.

The runner executes five suites, verifies thirteen rejected programs, checks
each public header in isolation and checks that unsafe floating optimization
flags are rejected. Sanitized runs enable address, undefined-behavior and
float-cast-overflow checks and stop on the first diagnostic. Compiler warnings
are errors. Each command's output is retained in a separate log.

JSON locale checks try a German numeric locale on Linux and Windows. Set
`TELEMETRY_TEST_LOCALE=de_DE.UTF-8` (Linux) or `German_Germany.1252` (Windows)
to require it: an unavailable requested locale fails the suite. Without that
variable, an unavailable locale is reported as a skipped check. GitHub Actions
installs and requires the locale. The test restores the previous locale.

The numeric oracle requires at least 64 bits of mantissa in `long double`
so it can represent all 64-bit integers exactly. It uses extended precision
and explicit `trunc` as an independent reference; the production code uses
source-precision comparisons before casting. Hosts without that precision
report the oracle as skipped; other suites still cover numeric endpoints.

## Audit checkpoint, 2026-09-19

Reviewed all active library files, the bundled delegate interfaces used by
Getter/Setter, the demo's owned lifetimes, read/write normalization, direct
lookup, public documentation and tests. Confirmed fixes:

- Null JSON output with positive capacity previously reached `snprintf` and
  reproduced an invalid write under AddressSanitizer. It now returns zero.
- A decimal-comma locale previously serialized 1.5 as `[1,5]`. The serializer
  now writes a decimal point without changing the application's locale.
- F32 value `nextafter(1.0f, 2.0f)` previously became JSON `1`. Nine significant
  digits preserve the original value. F64 retains seventeen digits.
- An exhausted output buffer previously continued calling getters. It now
  stops immediately, and every positive-size output stays NUL-terminated.
- CubeIDE's `newlib-nano/newlib.h` disables `_WANT_IO_LONG_LONG`. U64/S64 now
  use bounded decimal conversion independent of that printf feature, with
  unsigned arithmetic for the magnitude of INT64_MIN.

GCC 13.1 and Clang 18 passed 411 C++17 and 413 C++20 runtime checks; Clang
C++17 also passed with all three sanitizers. This includes 121 conversion
pairs, each with endpoints and 1024 samples, and JSON round trips with 4096
samples plus endpoints for each of four number types. Both compilers rejected
the invalid programs for the expected reasons. No findings remained in the
reviewed numeric conversion and direct-index implementations.

CubeIDE GCC 14.3.1 compiled the library, demo, tests and all five code-generation
probes for Cortex-M7 at `-O2`/`-Os` without exceptions or RTTI. The known matching
typed reads still branch directly to getters, and F32-to-U16 still checks two
source-precision bounds before one conversion. A minimal JSON consumer linked
with `nano.specs`, `nosys.specs` and `-Wl,-u,_printf_float`; the supplied nosys
system-call stubs generated their expected linker warnings. Neither that
consumer nor firmware was executed on a board during this audit. Its source is
[EmbeddedLinkCheck.cpp](EmbeddedLinkCheck.cpp), with linking flags in its header.

## Contracts the caller supplies

Passing invalid borrowed storage cannot be made safe by an index lookup.
Owners, arrays and strings must outlive their readers/writers, explicit counts
must describe actual array extents, and bound objects must keep their address.
Metadata stays immutable during use. The owner provides synchronization and
any coherent snapshot across fields. JSON metadata follows the documented
non-null string/identifier restrictions, output does not overlap inputs, and
the application does not change the process locale concurrently.

These checks establish behavior for those contracts on the tested toolchains.
They do not prove correctness of arbitrary application callbacks or concurrent
access, nor measure latency or total stack use on the device.
