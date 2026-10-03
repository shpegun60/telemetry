# H7S build support

Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.

`build.py` is the device-independent builder shared by the descriptor,
values-resource and optional protocol benches. It builds the selected fixture
against a captured LF source tree and a separate copy of a supplied Cube
scaffold. It never opens a serial port or invokes a programmer.

The scaffold must already contain the bench hooks, UART3 setup and 600 MHz
clock configuration. The builder uses CubeIDE ARM GCC 14.3.1, C++20,
Cortex-M7 hard float, no exceptions/RTTI, section GC and O2/Os without LTO.
Both complete images must fit the 64 KiB internal Flash range starting at
`0x08000000`. Image hashes, compiler identity, actual captured Git HEAD/dirty
state, source/scaffold hashes, objects, flags, maps, disassembly and individual
stack reports remain in the requested fresh artifact directory. Recorded
compiler frames are not summed call-chain or interrupt-stack measurements.

Each bench owns its protocol parser, correctness/timing coverage and optional
device session. Its `--run` requires an explicit programmer, adapter serial
and serial port, after both images have built. The session takes a fresh full
internal Flash backup, identifies the requested board, restores in `finally`
and verifies the complete read-back hash. No option bytes or external memory
are part of these sessions. Retained receipts in the bench folders describe
their historical source snapshots; this shared builder does not update them.
