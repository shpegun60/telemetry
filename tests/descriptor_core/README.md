# Descriptor core qualification

`CoreOnly.cpp` constructs, fingerprints, packs and reads a mixed native Model
using `telemetry/descriptor/Descriptor.hpp`. The runner inspects its compiler
dependency closure and refuses Resource or Client headers. Every byte offset is
read with empty, one-byte and larger output spans; output tails remain untouched.

`ResourceAdapter.cpp` compares core streaming reads with the actual Resource
`DescriptorFile` over streaming and packed sources. The mixed, enum/array/UTF-8
and empty fixtures cover every byte offset, untouched output tails, explicit
Resource status mapping and invalid-definition precedence. The runner also
compares all three emitted byte arrays with the frozen `.hex` goldens. No live
getter or endpoint is called; no compatibility Descriptor implementation exists.

```sh
python3 tests/descriptor_core/run.py --build-dir /tmp/telemetry-descriptor-core
python3 tests/descriptor_core/run.py --cxx clang++-18 --sanitize \
  --build-dir /tmp/telemetry-descriptor-core-sanitized
```

The build directory must be outside the checkout. Logs and `summary.json` retain
commands, runtime counts and source hashes. Existing independent descriptor
goldens/parser and Values provider regressions remain in
[`tests/structured/descriptor`](../structured/descriptor/README.md) and
[`tests/structured/resources`](../structured/resources/README.md).
