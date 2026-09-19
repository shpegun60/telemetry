# magic_enum dependency

Unmodified header and MIT license from
[magic_enum v0.9.8](https://github.com/Neargye/magic_enum/releases/tag/v0.9.8),
commit `1384769c66bd16ec9bb1353f45fe8ec8ccc12dbd`.

Header SHA-256:
`5130d8830a7a74bf15fded8db94b46b0adc2c1098461dd4b21051050fe2d841d`.
Git preserves its original bytes. The `.pri` exposes the files in Qt Creator;
no separate compilation or Qt dependency is required.

Only `TelemetryEnum.h` includes this header, via the sibling directory. Numeric
catalogs do not include reflection code. The upstream default scan range is
`-128..127`; specialize `magic_enum::customize::enum_range<E>` in the shared
enum definition header for another range, or use telemetry's explicit enum
value pack. Explicit values do not scan a range. All translation units must
use the same enum definitions and reflection configuration.

Aliased enumerators share a numeric code and a compiler-selected name. They
cannot produce distinct keys in one JSON dictionary. Forward declarations
alone do not provide enumerator names. Names are compiled into the program;
only JSON construction happens at runtime. No runtime registration is used.
On Clang, an enum nested in a class template may need a named enumerator use
before scanning, to instantiate its enumerator list. The explicit value pack
does this naturally; numeric type traits alone may not instantiate that list.
