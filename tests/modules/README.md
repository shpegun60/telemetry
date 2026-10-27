# Sealed CMake module qualification

Run `python -B tests/modules/run.py --build-dir <fresh-external-directory>`.
The runner installs each selected module, relocates the installation, then
builds and runs an independent consumer. `--modules client` selects a focused
run; `--arm --cxx arm-none-eabi-g++` compiles and links without execution.

The matrix includes the optional `tcp_server` integration. Its relocated consumer
assembles a real Resource LIST and TEQ1 request and links the existing dispatchers.
Negative compile checks reject Client, RPC, Runtime and Descriptor-resource adapter
headers from that target. Conversely every other independent module rejects TCP
adapter headers. A negative link control omits compiled dependencies and must fail
on the existing `dispatchEncoded` symbol. Focused host and offline ARM runs:

```sh
python3 -B tests/modules/run.py --build-dir /external/modules-tcp --modules tcp_server --cxx g++
python3 -B tests/modules/run.py --build-dir /external/modules-tcp-arm --modules tcp_server --cxx arm-none-eabi-g++ --arm
```

Header cleanup controls run before the module matrix. Fresh output, install-time
prefix overrides, DESTDIR on POSIX, internal directory links, a symlinked prefix
and owned leaf links must succeed. Intermediate links escaping the build or
installation root must fail before deletion or copying, with byte-identical
canaries retained. Both existing and missing destination leaves are tested;
`REAL_PATH` alone does not resolve intermediate links for a missing leaf.
On Windows it can also leave an existing directory junction unresolved. The
guard therefore expands every Windows link component on the host, separately
from the cross-compilation target, before checking ownership. NT/extended drive
and UNC names and relative link targets are supported; unsupported device names,
dangling/cyclic links and more than 64 expansions are refused.
Leaf-link targets inside the owner must retain their canaries when the link is
replaced. All cases are created under the external build directory.

Install-path controls also combine symlinks with `..` in include, library and
data directories, configure/runtime prefixes and POSIX DESTDIR. They verify the
actual header/archive/package/license locations and preserve sibling canaries.
Cleanup, exports and installation use the same lexical-normalized paths.
Relative runtime prefixes resolve against the installation invocation directory.
CMake's stripped root sentinel retains root semantics; an empty runtime CLI
override retains the configured prefix. Actual root
installs are tested only inside an external POSIX DESTDIR. Windows drive-root
normalization is tested with `cmake -P` without installing into a drive root.

Linux requires all symbolic-link controls. Windows additionally requires a
junction group using CPython's `CreateJunction`, without administrator or symlink
privileges: existing/missing escaping leaves, internal intermediate and leaf
junctions, a junctioned owner root, dangling/cyclic links and a bounded chain.
NT/UNC string conversion is tested without network access. The fixture finally
unlinks only its recorded junctions, leaving target files and canary hashes.
If ordinary symbolic-link creation is denied, that separate limitation is
recorded; the junction group and normal installed-consumer checks still run.
No hardware is exercised.
Commands, logs, canary hashes and before/after source hashes are retained in the
external directory and `summary.json`.
