# Production Encoded ReadMany qualification

```sh
python3 -B tests/read_many/run.py --cxx g++ --build-dir /external/read-many-gcc
python3 -B tests/read_many/run.py --cxx clang++-18 --sanitize --build-dir /external/read-many-asan
python3 -B tests/read_many/run.py --cxx arm-none-eabi-g++ --arm --build-dir /external/read-many-arm
python3 -B tests/read_many/run.py --cxx s390x-linux-gnu-g++ \
  --emulator qemu-s390x --sysroot /usr/s390x-linux-gnu --build-dir /external/read-many-big-endian
```

Use fresh external directories. The runner finds CMake and Ninja on PATH or
accepts `--cmake`/`--ninja`; nothing is deleted. Host checks execute Wire, Server,
Client, NoHeap and the example through a **relocated installed package**.
Two compile-fail controls reject forged bindings and insufficient callback space.
Six allocation controls must exit 86. Linux and MinGW GNU-style linkers wrap
malloc/calloc/realloc/free, in addition to C++ allocation traps.
The same complete host suite and example execute on the big-endian s390x target
under QEMU, including the fixed little-endian capability golden bytes and
bit-exact scalar replies. This is CPU emulation, not physical hardware.

Wire checks cover golden TRM1 bytes, truncation at every extent, count/order,
reserved/status/length failures and 4096 deterministically generated arbitrary
frames. Server checks exercise global preflight before getters, remaining aligned
workspace, scalar/enum eligibility, failures without partial value publication,
overlap, explicit capability and legacy exact-size behavior. Client checks use
real Encoded and RetryingEncoded backends, the sole FIFO, 32 items, bit-exact
floats/open enums/U64/S64, QueueFull, copied bindings, rejected whole replies,
queued/offered/prepared timeout, cancellation, invalidation, stale callbacks,
reentrancy and explicit zero-traffic RPC refusal.

`tests/modules/run.py` additionally seals every exported `read_many_client`
header and rejects Native, Codec, Resource, RPC, backend and Workbench dependencies.
Other modules reject this optional client header. Base DynamicClient still
rejects wire dependencies. The generic old dispatcher links without new source
files in its existing regression suites.

Offline Cortex-M7 checks build `-Os` and `-O2` selected legacy/batch roots from
installed archives, retain ELF/map/nm/objdump/stack records, and reject heap
symbols or batch-symbol leakage into the legacy root. The selected sections and
individual compiler stack frames are **not** full-firmware Flash deltas or peak
call-chain stack measurements. Images are never executed or flashed.

`summary.json` records tool/source/log hashes, source identity, real executions,
installed-header stability and commands. Hardware is always false. Production
RPC, JS/TS batch calls, GUI watchlists and Nucleo ReadMany are outside this stage.
