# magic_enum dependency

Unmodified header and MIT license from
[magic_enum v0.9.8](https://github.com/Neargye/magic_enum/releases/tag/v0.9.8),
commit `1384769c66bd16ec9bb1353f45fe8ec8ccc12dbd`.

Header SHA-256:
`5130d8830a7a74bf15fded8db94b46b0adc2c1098461dd4b21051050fe2d841d`.
License SHA-256 (canonical tracked Git blob bytes):
`bd227b8a5586dc73012262abfc0fc4eb84c2a91ad3f93b3591f8148fe17324d3`.
Git preserves its original bytes. The `.pri` exposes the files in Qt Creator;
no separate compilation or Qt dependency is required.

The C++20 reflection backend includes this header. Registry, Codec, Model and
resource providers consume telemetry's normalized reflection facade rather
than calling magic_enum directly. Its default scan range is `-128..127`.
Specialize `magic_enum::customize::enum_range<E>` in a shared enum definition
header for another range, or specialize `telemetry::reflection::EnumReflection<E>`
with `enumCodes` / `enumEntries` for an explicit sparse or filtered dictionary.
All translation units must use the same enum definitions and configuration.

Aliased enumerators share a numeric code and one compiler-selected automatic
name. Explicit dictionary entries select one name per distinct code. Forward
declarations alone do not provide enumerator names. Reflection runs at compile
time; no runtime registration or JSON formatting is introduced by this header.
See the supported customization syntax in the
[user guide](../../doc/user/NativeApi.md).
