# User-guide example checks

`run.py` compiles the four complete examples under
[`examples/user_guide`](../../examples/user_guide) with C++20 and warnings as
errors, plus the complete multi-file
[`device_integration`](../../examples/device_integration/README.md) application.
Host mode executes all five programs. `Native.cpp` keeps its runtime
assertions enabled through `-UNDEBUG`, as does `QuickStart.cpp`; a preprocessor
control verifies that `NDEBUG` is absent and `assert` remains defined. The Native example reports
completion, while Resources, Encoded and DeviceIntegration report their
executed check counts.
The runner records those counts without claiming per-assert counts for Native
or QuickStart. It also requires exact text agreement between `QuickStart.cpp`
and the root README's marked `quickstart:begin` / `quickstart:end` C++ block,
normalizing only platform line endings.

```sh
python tests/docs/run.py --cxx g++ --build-dir build/docs
python tests/docs/run.py --cxx clang++-18 --sanitize --build-dir build/docs-san
python tests/docs/run.py --cxx g++ --null-checks --build-dir build/docs-null
python tests/docs/run.py --cxx arm-none-eabi-g++ --arm --build-dir build/docs-arm
```

Select the actual compiler on the machine. `--build-dir` is required and may
point outside the checkout. Sanitizers are a host execution mode. ARM mode
compiles and links all five programs at `-O2` and `-Os` with newlib-nano and
nosys; it executes no image and connects to no board. `--null-checks` retains
the existing null-check compiler mode in either host or ARM builds.

Each command writes its arguments, exit status, elapsed time and output to a
log. `summary.json` records compiler and input hashes, image hashes, enabled
assertions, build/execution counts, reported conditions and completion state.
All generated files stay under the supplied output directory. QuickStart,
Native and Encoded link the compiled telemetry ABI and Model adapter.
Resources links only the generic resource packet implementation.
DeviceIntegration links both compiled telemetry sources and the generic
resource processor. Its counted checks exercise native operations, canonical
payloads, resource requests and bounded split/coalesced frame reception.
The project's header and CMake input hashes are recorded alongside its C++
source hashes. The same executable can also be built with the example's
CMakeLists.txt and run through CTest; the docs runner covers it directly in
all existing CI modes.
