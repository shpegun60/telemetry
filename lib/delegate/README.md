# tiny_delegate dependency

This directory contains the header and MIT license from
[tiny_delegate v1.2.0](https://github.com/shpegun60/delegate/releases/tag/v1.2.0),
commit `5143bfa6ae2372b60f387feca57a69f7a55169db`.

`delegate.pri` adds the header and its include directory to a qmake consumer.
Telemetry's compact Getter/Setter implementation does not depend on this header.
Its `DelegateRefSlot` and `DelegateSlot` do use it; `telemetry.pri` includes this
dependency for the complete public umbrella. No Qt dependency, separate
compilation or machine-specific path is required.

The upstream header SHA-256 is
`4664d6d36ea8f0b2b38286473e1a244fb74ccb992dafa22b61a4376d4171f359`.
Line-ending conversion can change this byte-level hash.

For an update, copy the header and license from one verified upstream release
and update this provenance. All translation units in a program must use the
same header revision and configuration macros.

Version 1.2.0 includes the former telemetry-side context-binding primitive as
the upstream `delegate_ref::bind_context<&adapter>(object)` API. This bundled
copy has no local source changes.
