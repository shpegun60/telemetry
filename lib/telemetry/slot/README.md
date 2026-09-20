# Runtime binding slots

Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT; see [LICENSE](../LICENSE).

Tables borrow a slot at a stable address. Bind it after constructing application
state, replace its target, or reset it without rebuilding the table or schema.
All five types are noncopyable and nonmovable, with `bind`, `reset`, `available`
and `explicit operator bool`. No runtime mode chooses between their strategies.

| Type | Stores | Owns target | ARM32 storage |
| --- | --- | --- | --- |
| `OwnerSlot<T>` | `T*` | No | 4 bytes |
| `FunctionSlot<Sig>` | Function pointer | No | 4 bytes |
| `ContextFunctionSlot<Sig>` | Function pointer and `void*` | No | 8 bytes |
| `DelegateRefSlot<Sig>` | `tiny::delegate_ref` | No | 8 bytes |
| `DelegateSlot<Sig, Bytes, Align>` | `tiny::delegate` with inline storage | Yes | Payload plus delegate overhead/alignment |

`Sig` is `R(Args...) noexcept`. The default owned capacity is 32 bytes and its
default alignment is `alignof(std::max_align_t)`; both are template parameters.
Callback invocation must be nothrow. Callback parameter/value-result types must
match the slot signature exactly; bind does not silently narrow numbers inside
a delegate. Compatible reference results and discarding a result for `void`
are supported. A concrete or signature-resolvable generic/overloaded call
operator is accepted; otherwise use an explicit typed lambda adapter.
Owned callable construction from the given
source, movement and destruction must also be nothrow and fit the chosen size
and alignment. Oversized targets are rejected even if a consumer enables the
companion delegate library's heap fallback. Slots never allocate target storage.

## One field/command interface

```cpp
using namespace telemetry;
inline ContextFunctionSlot<float() noexcept> contextRead;
inline DelegateRefSlot<float() noexcept> borrowedRead;
inline DelegateSlot<float() noexcept, 32> ownedRead;
inline DelegateSlot<WriteResult(float) noexcept, 32> ownedWrite;
inline DelegateSlot<CommandResult(float) noexcept, 32> configure;

inline constexpr FieldTable fields{
    field("Context", "V", contextRead),
    field("Borrowed", "V", borrowedRead),
    field("Owned", "V", ownedRead, ownedWrite, limits(230.f, 0.f, 500.f)),
};
inline constexpr CommandTable commands{
    command("Configure", configure, arg<0>("Value", "V", 230.f, 0.f, 500.f)),
};
```

For context callbacks, the actual function receives an extra first `void*`
argument. The context is borrowed; null is permitted when the callback can
handle it. Availability depends on the function pointer alone.

```cpp
contextRead.bind(+[](void* p) noexcept {
    return static_cast<Meter*>(p)->voltage();
}, &meter);
borrowedRead.bind<&Meter::voltage>(meter);
// Or borrow a stable named closure; the closure must outlive its binding.
auto getter = [&meter]() noexcept { return meter.voltage(); };
borrowedRead.bind(getter);

// Owned closure: correction is copied; meter is still borrowed.
ownedRead.bind([correction = 1.02f, &meter]() noexcept {
    return meter.voltage() * correction;
});
ownedWrite.bind([&meter](float value) noexcept { return meter.setLimit(value); });
configure.bind([&meter](float value) noexcept { return meter.configure(value); });

auto value = fields.read<2>();         // optional<float>; native callback.
auto write = fields.write<2>(250);     // Checked int -> float conversion.
auto result = commands.call<0>(250.);  // Checked double -> float; no Scalar array.
ownedRead.reset();                    // Subsequent reads return nullopt.
```

The same slots work through global catalog tables and runtime indexes. Getter
and setter slots may be different kinds, but their native value types must
match exactly. Enum inference, dictionary metadata, numeric limits and explicit
Scalar callback forms work as for other field/command definitions. Direct
objects, free functions and ordinary borrowed callables acquire no slot checks.

`DelegateRefSlot` additionally supports `bind<&freeFunction>()`, `bind(function)`
and capture-free temporary lambdas converted to function pointers. Method owners
are stable object lvalues. Named stateful/capturing callables are borrowed without
copying. `DelegateSlot` copies lvalue callables or moves rvalues, including
move-only closures. It intentionally has no borrowing/method-owner overload:
capture an owner reference in its owned closure, or use `OwnerSlot`/`DelegateRefSlot`.

## Absence and lifetime

Slots do not assign field/command meanings to absence. Telemetry checks their
presence before invoking a callback: an empty getter produces Scalar Null or
`nullopt`, and an empty setter/command reports `Unavailable`. A field with no
setter remains `ReadOnly`. Field conversion/limits precede setter availability;
command transport shape checks precede availability, which precedes conversion.
Binding/reset does not change a descriptor's declared capability or schema CRC.

Callable slots expose `get()` snapshots/views for adapters and `invoke()` with
the precondition that the target is engaged. There is no `operator()` accepting
an unchecked call. A function/context/ref snapshot keeps its selected target;
an owned snapshot is a view and does not copy/extend the owned target's lifetime.

Every slot must outlive all referring tables and active calls. `OwnerSlot`,
context slots and borrowed delegates do not extend any target's lifetime.
An owned delegate owns the closure object, not objects captured by reference.
Capture short-lived values by value; reset borrowed references before destroying
their objects. Temporary slots and borrowed target temporaries are rejected.

Bind/reset, destruction and access require external serialization. In particular,
an owned callback must not reset or replace its own slot while executing: that
would destroy its executing closure. Replace it after the call returns. No locks,
atomic counters or deferred deletion are inserted on any slot's invocation path.

`DelegateRefSlot` and `DelegateSlot` use the unmodified bundled tiny_delegate
header. Its configuration macros must agree in all translation units, as when
using tiny_delegate directly. Include the individual non-delegate slot headers
when a consumer wants that subset without including the delegate header.
