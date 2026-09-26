# Pinned Boost.PFR source

This directory contains the `include/boost` tree and license from the
[Boost.PFR repository](https://github.com/boostorg/pfr), tag
`boost-1.92.0`, commit `401385c240027423acbb1eb6dea2abe0043db5aa`.
The repository root's `LICENSE_1_0.txt` is copied unchanged. The 44 header
files were compared byte-for-byte against a clean checkout of that commit.
No other Boost module is vendored or required by these headers.

The canonical tracked header tree digest is SHA-256
`4c9837f5af31e83ef9e4d1c84a57588dcd7128ecfcc18a2445b24f82b246cfaf`.
It is computed from Git blob bytes by sorting paths relative to `include`,
then feeding each UTF-8 path, one zero byte and the 32 raw bytes of that
blob's SHA-256 digest to one SHA-256 stream. The tracked license blob's
SHA-256 is
`c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566`.
These are independent of CRLF conversion in a Windows working tree.

The existing `magic_enum` remains at v0.9.8, as recorded in
[`../magic_enum/README.md`](../magic_enum/README.md). Its default automatic
scan range is `-128..127`. Its version and configuration are independent
of this Boost.PFR pin.
