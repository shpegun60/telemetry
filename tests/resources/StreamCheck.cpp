// Every byte offset, tiny chunks, errors and atomic callback preflight (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "TestSupport.hpp"
#include <resource/telemetry/detail/BlockStream.hpp>
using namespace telemetry_resource::detail;
using resource::Status;
unsigned calls = 0;

resource::ReadResult read(resource::Cursor cursor, resource::Output output)
{
    BlockStream stream{cursor, output};
    while (stream.active())
    {
        if (stream.key() != 0)
        {
            stream.fail(Status::InvalidCursor);
            break;
        }
        if (stream.kind() == BlockKind::Prefix)
        {
            if (!stream.rawRecord(3,
                                  [](OutputWriter& out) noexcept
                                  {
                                      return out.raw("abc");
                                  }))
            {
                break;
            }
            stream.finish(pack(BlockKind::Catalog));
        }
        else if (stream.kind() == BlockKind::Catalog)
        {
            if (!stream.record(7, 7,
                               [](OutputWriter& out) noexcept
                               {
                                   return out.string("A\nB");
                               }))
            {
                break;
            }
            stream.finish(pack(BlockKind::Entry));
        }
        else
        {
            if (!stream.atomic(9,
                               [](resource::Output out) noexcept
                               {
                                   ++calls;
                                   std::fill(out.begin(), out.end(), std::byte{42});
                               }))
            {
                break;
            }
            stream.finish(endCursor);
        }
    }
    return stream.result();
}

int main()
{
    const auto expected = unhex("616263 0701000007000000 03000000 410a42 2a2a2a2a2a2a2a2a2a");
    for (unsigned record = 0; record < 3; ++record)
    {
        const unsigned widths[]{3, 15, 9}, starts[]{0, 3, 18};
        for (unsigned offset = 0; offset <= widths[record] + 1; ++offset)
        {
            for (unsigned capacity = 0; capacity <= 40; ++capacity)
            {
                std::array<std::byte, 42> output;
                output.fill(std::byte{0xa5});
                const auto cursor = pack(static_cast<BlockKind>(record), 0, offset);
                calls = 0;
                const auto result = read(cursor, {output.data() + 1, capacity});
                CHECK(output.front() == std::byte{0xa5} && output[capacity + 1] == std::byte{0xa5});
                if (offset > widths[record] || (record == 2 && offset != 0))
                {
                    CHECK(result.status == Status::InvalidCursor && result.next == cursor &&
                          result.written == 0 && calls == 0);
                }
                else if (result.status == Status::Ok)
                {
                    CHECK(result.written <= capacity && calls <= 1);
                    CHECK(std::equal(output.begin() + 1, output.begin() + 1 + result.written,
                                     expected.begin() + starts[record] + offset));
                    CHECK(result.written != 0 || result.next != cursor || result.eof);
                }
                else
                {
                    CHECK(result.status == Status::BufferTooSmall && result.written == 0 &&
                          calls == 0);
                }
            }
        }
    }
    std::byte output[40]{};
    CHECK(read(endCursor, {}).eof);
    for (auto bad : {pack(BlockKind::End, 0, 1), pack(BlockKind::End, 1),
                     pack(BlockKind::Prefix, 1), pack(BlockKind::Catalog, 65536), UINT64_MAX})
    {
        calls = 0;
        CHECK(read(bad, output).status == Status::InvalidCursor && calls == 0);
    }
    BlockStream failed{0, output};
    CHECK(!failed.record(1, 1,
                         [](OutputWriter& out) noexcept
                         {
                             return out.fail();
                         }));
    CHECK(failed.result().status == Status::InvalidData);
    // Supplied arithmetic lengths are checked against a completed encoding.
    // Each READ encodes once; records preceding the local offset encode zero times.
    for (const auto payload : {1u, 3u})
    {
        BlockStream mismatch{0, output};
        unsigned encoded = 0;
        CHECK(!mismatch.record(1, payload,
                               [&](OutputWriter& out) noexcept
                               {
                                   ++encoded;
                                   return out.raw("AB");
                               }));
        const auto result = mismatch.result();
        CHECK(encoded == 1 && result.status == Status::InvalidData && result.next == 0 &&
              result.written == 0);
    }
    unsigned encoded = 0;
    const auto pair = [&](OutputWriter& out) noexcept
    {
        ++encoded;
        return out.raw("AB");
    };
    BlockStream once{0, output};
    CHECK(once.record(1, 2, pair) && encoded == 1 && once.finish(endCursor));
    BlockStream skip{10, output};
    CHECK(skip.record(1, 2, pair) && encoded == 1 && skip.finish(endCursor));
    BlockStream partial{8, resource::Output{output, 1}};
    CHECK(!partial.record(1, 2, pair) && encoded == 2);
    CHECK(partial.result().status == Status::Ok && partial.result().written == 1 &&
          partial.result().next == 9);
    BlockStream zero{0, output};
    CHECK(!zero.atomic(0,
                       [](resource::Output) noexcept
                       {
                       }));
    CHECK(zero.result().status == Status::InvalidData);
    unsigned emitted = 0;
    const auto emit = [&](OutputWriter&) noexcept
    {
        ++emitted;
        return true;
    };
    BlockStream maximum{pack(BlockKind::Prefix, 0, offsetMask), {}};
    CHECK(maximum.rawRecord(offsetMask, emit) && maximum.finish(endCursor));
    CHECK(maximum.result().eof && emitted == 0);
    BlockStream tooLarge{0, output};
    CHECK(!tooLarge.rawRecord(offsetMask + 1, emit));
    CHECK(tooLarge.result().status == Status::InvalidData && emitted == 0);
    BlockStream aggregate{pack(BlockKind::Prefix, 0, offsetMask), {}};
    CHECK(aggregate.rawRecord(offsetMask, emit) && !aggregate.rawRecord(1, emit));
    CHECK(aggregate.result().status == Status::InvalidData && emitted == 0);
    std::printf("Hierarchical block stream: %u checks\n", checks);
}
