# Final-source resources H7S evidence

Authors: Ruslan Kovtun (shpegun60), codexAi. Date: 2026-10-04.

The [receipt](receipt.json) records actual execution of `7b73fb4c97e48ebb012ea0b6010bc7a4c3c434b3`
on NUCLEO-H7S3L8, selected ST-LINK `002A001F3033510135393935`, COM6.
CubeIDE ARM GCC 14.3.1; 600 MHz Cortex-M7 with caches enabled.
`source_dirty=true` is retained; captured input hashes identify the build.
This run qualifies the borrowed-result extension and is distinct from
the [historical receipt](../../../../doc/evidence/pre-unification/stage10/receipt.json).

Both O2/Os images passed their full correctness and timing plans. All
loadable sections were bounded to internal Flash before device access.
Every write was verified; all 65536 Flash bytes were restored and freshly
read back with matching SHA-256:

`a5903024dba85fab5121150ca8ad13482f97384aa450aab67413881991fb9456`

The original image was reset and left running. Option bytes/external
memory were not changed. Local retained images, sources, logs and backup/
readback are under `build/borrowed/h7s-own-resources-live`. This path is not
a permanent artifact archive. The neutral build/helper contract is in
[h7s_support](../../../h7s_support/README.md).

The adjacent `verify.py --self-test` performs offline receipt/mutation
validation. It does not operate the device or establish a fresh run.

The [Stage 20 baseline receipt](../../../../doc/evidence/pre-borrowed/h7s/resources/receipt.json) retains its original source identity. This new receipt does not relabel that measurement.
