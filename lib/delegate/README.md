# tiny_delegate dependency

This directory contains the header and MIT license from
[tiny_delegate v1.1.0](https://github.com/shpegun60/delegate/releases/tag/v1.1.0),
commit `a95a9d772570932511ff57325d33ab38f411a624`.

`delegate.pri` adds the header and its include directory to a qmake consumer.
The sibling telemetry library includes it automatically. No Qt dependency,
separate compilation or machine-specific path is required.

The upstream header SHA-256 is
`82af1433730d1f7224c07b21c1b0d0b5e0941b84d171faa9f137b85bd58ffa68`.
Line-ending conversion can change this byte-level hash.

For an update, copy the header and license from one verified upstream release
and update this provenance. All translation units in a program must use the
same header revision and configuration macros.

Telemetry carries one local extension: `delegate_ref::bind_context<Function>(object)`
binds a noexcept-checked telemetry adapter to an lvalue context without a closure
allocation. The delegate layer itself preserves its general callable policy.
Object/function pointer representations remain separate; storage is still two
pointers and existing call paths are unchanged. Temporary contexts are rejected.
The telemetry factory/command suites and ARM probes cover this extension.
Copying an upstream update must preserve or upstream this local extension.
