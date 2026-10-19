# Sealed CMake module qualification

Run `python -B tests/modules/run.py --build-dir <fresh-external-directory>`.
The runner installs each selected module, relocates the installation, then
builds and runs an independent consumer. `--modules client` selects a focused
run; `--arm --cxx arm-none-eabi-g++` compiles and links without execution.

Header cleanup controls run before the module matrix. Fresh output, install-time
prefix overrides, DESTDIR on POSIX, internal directory links, a symlinked prefix
and owned leaf links must succeed. Intermediate links escaping the build or
installation root must fail before deletion or copying, with byte-identical
canaries retained. Both existing and missing destination leaves are tested;
`REAL_PATH` alone does not resolve intermediate links for a missing leaf.
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

Linux requires all symbolic-link controls. On Windows, a denied symbolic-link
creation is recorded explicitly as unsupported; ordinary configure, install,
relocation and header-isolation checks still run. No hardware is exercised.
Commands, logs, canary hashes and before/after source hashes are retained in the
external directory and `summary.json`.
