# Canonical descriptor core

`Descriptor.hpp` describes immutable native `telemetry::Model` metadata without
including Resource or Client. It builds a segment index once, computes the
canonical TDS3 fingerprint once, and emits byte slices without sampling endpoints
or allocating memory. `Format.hpp` owns descriptor wire constants;
`ReadResult.hpp` owns its transport-independent byte-read result.

```cpp
#include <telemetry/descriptor/Descriptor.hpp>

constexpr telemetry::descriptor::Descriptor descriptor{model};
constexpr auto bytes = telemetry::descriptor::packDescriptor<descriptor>();
auto result = descriptor.read(offset, output);
```

`Cursor` is a 64-bit byte offset. `ReadStatus` reports `Ok`, `InvalidCursor`,
`BufferTooSmall` or `InvalidData`. Invalid metadata takes precedence over cursor
and buffer checks; EOF succeeds even with an empty output span. Reads preserve
the cursor and write no bytes on refusal. Definition errors, profile limits,
fingerprint inputs and emitted bytes preserve the frozen descriptor format.

The descriptor owns only its header and segment index. The Model tables, names
and metadata remain borrowed and immutable. Views also borrow the descriptor.
Output storage must be disjoint from all borrowed metadata. The native Model
and its existing reflection support are required at build time; Resource, Client
and OS libraries are not required.

`integration/telemetry/v3/DescriptorFile.hpp` is the actual Resource adapter. It
accepts core descriptors/views or packed arrays, and explicitly maps core reads
to `resource::ReadResult`. There is one Descriptor implementation and one wire
format definition. Former Resource include entry points and Integration
Descriptor facades are deliberately not provided. Values publishing belongs to
Integration; the core does not own live Values or Resource framing.
