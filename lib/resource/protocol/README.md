# Resource packets v1 (C++20)

Authors: Ruslan Kovtun (shpegun60), codexAi. [MIT](../LICENSE).

The `protocol` module depends only on the [resource core](../README.md).
`process(view, request, response)` accepts one complete request and returns
`Reply{status, written}`. The caller provides framing, transport, checksums and
retry policy. No telemetry, session storage, dynamic allocation or path lookup
is performed. Include `lib/resource/resource.pri`, or compile `Protocol.cpp`
with `lib` on the include path. The public header is
`<resource/protocol/Protocol.hpp>`; the namespace is `resource_protocol`.

All integers are encoded explicitly little-endian, without struct layout or
unaligned typed loads. There is no terminator after paths or data. Boolean
bytes are exactly zero or one. Extra trailing input bytes are invalid.

| Op | Request | Response |
|---|---|---|
| LIST = 1 | `u8 op, u64 cursor` | `u8 status, u64 next, u8 eof, u16 dataSize, data[]` |
| STAT = 2 | `u8 op, u32 index` | `u8 status, u32 size, u8 flags` |
| READ = 3 | `u8 op, u32 index, u64 cursor` | `u8 status, u64 next, u8 eof, u16 dataSize, data[]` |
| WRITE = 4 | `u8 op, u32 index, u64 cursor, u8 final, u16 dataSize, data[]` | `u8 status, u64 next, u32 consumed, u8 complete` |

Status codes are fixed: Ok=0, InvalidFile=1, NotReadable=2, NotWritable=3,
InvalidCursor=4, CursorExpired=5, BufferTooSmall=6, InvalidData=7,
InternalError=8. STAT flags are Readable=1 and Writable=2. Existing codes
are not renumbered. `consumed` remains u32 as in the revised specification;
it is checked against the actual u16-length request payload.

LIST data consists of repeated `[u16 pathLength][path bytes]` entries. The
index of the first entry is the request cursor; each following entry increments
it by one. LIST never calls provider `size()`. A cursor equal to fileCount is
valid EOF; a larger cursor is invalid. A whole path entry must fit the available
payload after the 12-byte header. With some entries emitted, a full buffer
returns Ok and the first unreturned index. If the first entry cannot fit, it
returns BufferTooSmall with an unchanged cursor. A path length above 65535
cannot be represented and returns InvalidData. Because the complete data size
is u16, the largest individually transferable LIST path is 65533 bytes.

READ payload capacity is min(response size - 12, 65535). WRITE validates the
entire packet and the 14-byte reply capacity before invoking the provider.
Providers are invoked synchronously; in-place request/reply storage is supported.
Malformed packets, invalid final bytes and unknown operations produce a single
InvalidData status byte if possible. Valid requests retain the operation's
normal response envelope on errors. If that envelope cannot fit, no provider
is invoked and `Reply{BufferTooSmall, 0}` tells the transport to enlarge its
buffer. Provider result counts/statuses are checked before encoding; inconsistent
results become InternalError. This cannot repair a provider that has already
written outside its C++ span: that remains a provider contract violation.

WRITE retries are delivered again. There is no transaction or deduplication
layer. Clients check status, byte count, progress and completion on every reply.

See [tests](../../../tests/resources/README.md) and the small
[application facade](../../../app/resources/DeviceResources.hpp).
