# Structural descriptor v3.0 (C++20)

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../LICENSE).

Stage 09 implements the immutable descriptor of a structured `Model`.
It does not introduce resource framing, Bind, Exchange or ValuesFile; their
integration remains in the following stages. There is no separate `.pri`
or telemetry dependency in the generic resource core.

```cpp
#include <resource/structured/Descriptor.hpp>

// fields, commands and services are stable structured catalog tables.
inline constexpr telemetry::structured::Model model{fields, commands, services};
inline constexpr resource::structured::Descriptor descriptor{model};

// Optional alternative for immutable compile-time metadata: only the final
// byte array needs to survive linking when the streaming object is unused.
inline constexpr auto descriptorBytes =
    resource::structured::packDescriptor<descriptor>();
```

The model and names are borrowed. Tables and name storage must outlive the
descriptor and remain immutable. Owner state and slot targets may change;
neither is inspected by construction, hashing or reading. A descriptor view
also borrows its owning descriptor; obtaining a view from a temporary is
rejected. Copying a descriptor does not copy or own the underlying tables.

## Format and identity

The [implementation plan, section 10](../../../doc/StructuredTelemetryV3ImplementationPlan.md#descriptor)
is the wire contract. The file contains a 64-byte `TDS3` header, then Type,
Catalog and Field/Command/Service records. Integers are canonical little-endian;
strings are a u32 byte length followed by UTF-8 without NUL. Wire bytes contain
no pointers, C++ padding, unit, limits, defaults, policy flags or type names.
Every endpoint record ends with its name.

The one u64 fingerprint is FNV-1a over the entire descriptor with header
bytes 16–23 treated as zero. It is computed once during construction, or
at compile time for a constexpr object. `read()` and `fingerprint()` never
recalculate it. Field writable capability comes from its definition type,
not the current availability of a setter slot or weak function address.

Changing shapes, dictionaries, positions, names or writable capability changes
the descriptor bytes and normally the fingerprint. Owner/callback addresses,
live values and slot availability do not. A hash collision remains possible;
the fingerprint is not a payload checksum or an access-control mechanism.

## Streaming and storage

```cpp
std::array<std::byte, 128> out;
resource::Cursor cursor = 0;
auto result = descriptor.read(cursor, out);
// Consume out[0 .. result.written), then continue with result.next.
```

Here the opaque resource cursor is a byte offset. Reading may stop inside a
record or UTF-8 string; the reconstructed file is identical to a single read.
At `size()` the result is `Ok`, zero bytes and EOF. Beyond `size()` it is
`InvalidCursor`. An empty buffer before EOF gives `BufferTooSmall`. Errors
preserve the cursor and leave the output untouched.

The streaming descriptor owns a 12-byte index entry per segment plus one
sentinel. A segment is a header, type prefix, member, enum entry, catalog or
endpoint. It locates a resume position by binary search, then emits only
visited segments. Skipping a string prefix does not rescan it. Reading costs
O(log M + emitted bytes + visited segments), with bounded stack and no heap.
Construction validates names, sorts name indexes in place to check uniqueness,
restores positional order, computes offsets and hashes once. No complete
serialized duplicate is created in RAM.

For a constexpr model, **packed bytes in Flash are the recommended storage**:
the [measured fixture](../../../tests/structured/descriptor/README.md) needed
less Flash and fewer cycles than on-demand emission. `packDescriptor` returns
an ordinary constexpr `std::array<std::byte, N>`; it uses the same canonical
emitter as streaming. Use one representation for a provider. The streaming
form remains useful when names/catalog metadata are constructed at startup.
No explicit `init()` phase is required. The resource provider wrapper for
either representation belongs to Stage 10.

## Validation and limits

Names are nonempty valid UTF-8. Factory arguments preserve const array extent
long enough to reject embedded NUL in literals; mutable character buffers and
char pointers use bounded C-string semantics. A pointer must reference a valid
NUL-terminated string. Text after its first terminator is not part of that
string. Names are stored as the same single borrowed pointer as before; native
endpoint dispatch gains neither a member nor a runtime name check.

Catalog names are unique within their category; endpoint names are unique
within their catalog. Cross-category duplicates are valid. In a constexpr
descriptor, an invalid declaration produces a construction diagnostic. At
runtime descriptor validation reports `DescriptorError`, size/fingerprint zero
and `InvalidData` on read; invalid factory names themselves remain contract
violations. `Descriptor<F,C,S,Profile>` may tighten `structured::Limits` for
type count/depth, members, arrays, enum entries, strings, wire bytes, expanded
nodes, descriptor bytes and total catalogs/endpoints. It does not extend the
underlying registry's supported type set or default ceilings.

The independent host parser fixture checks header budgets before allocating
record collections, validates every reference and recomputes wire sizes,
depth and expanded nodes without expanding arrays. It is a test oracle;
the application client is scheduled for Stage 12.
