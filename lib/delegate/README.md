# tiny_delegate dependency

This directory contains the header and MIT license from
[tiny_delegate v1.2.1](https://github.com/shpegun60/delegate/releases/tag/v1.2.1),
commit `813acab020fe9ef28fe7024989fb8d99c09a1b43`, with the local owning-assignment
correction described below. The license remains unchanged.

`delegate.pri` adds the header and its include directory to a qmake consumer.
Telemetry's compact Getter/Setter implementation does not depend on this header.
Its `DelegateRefSlot` and `DelegateSlot` do use it; `telemetry.pri` includes this
dependency for the complete public umbrella. No Qt dependency, separate
compilation or machine-specific path is required.

The bundled header SHA-256 is
`41d4e028ae823abc3a06c9dc4bac630ba6a356d0b0dfba88cbff23fe137d909e`.
Line-ending conversion can change this byte-level hash.

Local audit correction relative to v1.2.1:

- Prepare an owning replacement before destroying the previous target, including
  a callable subobject or a nested same-type delegate used as the source.
- Construction or allocation failure retains the previous owned target. As with
  ordinary C++ construction, effects performed by a user-supplied constructor
  on its source are outside this guarantee.
- The temporary uses the delegate's bounded storage; heap policy, object layout
  and invocation stay unchanged. Ordinary inline assignment adds one noexcept
  target move. Borrowed targets still require backing storage to outlive them.

Maintained checks are `tests/regression/DelegateInlineCheck.cpp` and
`tests/regression/DelegateHeapCheck.cpp`, sharing `tests/delegate/AssignmentChecks.hpp`.

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
