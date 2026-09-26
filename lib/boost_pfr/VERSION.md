# Pinned Boost.PFR source

This directory contains the `include/boost` tree and license from the
[Boost.PFR repository](https://github.com/boostorg/pfr), tag
`boost-1.92.0`, commit `401385c240027423acbb1eb6dea2abe0043db5aa`.
The repository root's `LICENSE_1_0.txt` is copied unchanged. The 44 header
files were compared byte-for-byte against a clean checkout of that commit.
No other Boost module is vendored or required by these headers.

The header tree digest is SHA-256
`81f3575c2478a6b652501ed1faac6adc3079d12ed5a78a289e2abafbe6be127a`.
It is computed by sorting paths relative to `include`, then feeding each
UTF-8 path, one zero byte and the 32 raw bytes of that file's SHA-256 digest
to one SHA-256 stream. The copied license SHA-256 is
`beb8e42e9d6b4284e03304d05a81a0755200a965fc8d0a5e0aea1e84cf805d6e`.

The existing `magic_enum` remains at v0.9.8, as recorded in
[`../magic_enum/README.md`](../magic_enum/README.md). Its default automatic
scan range is `-128..127`. Its version and configuration are independent
of this Boost.PFR pin.
