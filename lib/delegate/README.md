# tiny_delegate dependency

This directory contains the header and MIT license from
[tiny_delegate](https://github.com/shpegun60/delegate/tree/4a570f8d36e3bae72c02ec431b0918cb439e1be4),
commit `4a570f8d36e3bae72c02ec431b0918cb439e1be4`, the untagged 2026-10-18
audit follow-up to v1.2.0. There are no local header changes relative to that
commit.

`delegate.pri` adds the header and its include directory to a qmake consumer.
Telemetry's compact Getter/Setter implementation does not depend on this header.
Its `DelegateRefSlot` and `DelegateSlot` do use it; `telemetry.pri` includes this
dependency for the complete public umbrella. No Qt dependency, separate
compilation or machine-specific path is required.

The bundled header SHA-256 is
`b9bc65327969a766ed3e076f2665add228145b6349508e30ada02222ed4d2e69`.
Line-ending conversion can change this byte-level hash.

Upstream audit corrections relative to v1.2.0:

- Preserve a constructible source category when replacing an aliased heap
  target, including mutable-lvalue-only copies and volatile-rvalue moves.
- Return `false` directly from `uses_heap()` when heap fallback is disabled,
  avoiding a redundant indirect manager call.

For an update, copy the header and license from one verified upstream revision
and update this provenance. All translation units in a program must use the same
header revision and configuration macros.

Version 1.2.0 includes the former telemetry-side context-binding primitive as
the upstream `delegate_ref::bind_context<&adapter>(object)` API.
