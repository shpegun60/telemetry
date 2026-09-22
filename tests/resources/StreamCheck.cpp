// Every byte offset, tiny chunks, errors and atomic callback preflight (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "TestSupport.hpp"
#include <resource/telemetry/detail/BinaryStream.hpp>
using namespace telemetry_resource::detail;
using resource::Status;
unsigned calls = 0;

resource::ReadResult read(resource::Cursor cursor, resource::Output output)
{
    BinaryStream stream{cursor, output, 3};
    if (!stream.rawRecord(3,
                          [](BinaryWriter& out) noexcept
                          {
                              return out.raw("abc");
                          }))
    {
        return stream.result();
    }
    if (!stream.record(7,
                       [](BinaryWriter& out) noexcept
                       {
                           return out.string("A\nB");
                       }))
    {
        return stream.result();
    }
    (void)stream.atomic(9,
                        [](resource::Output out) noexcept
                        {
                            ++calls;
                            std::fill(out.begin(), out.end(), std::byte{42});
                        });
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
                const auto cursor = pack(record, offset);
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
    CHECK(read(pack(3), {}).eof);
    for (auto bad : {pack(3, 1), pack(4), UINT64_MAX})
    {
        calls = 0;
        CHECK(read(bad, output).status == Status::InvalidCursor && calls == 0);
    }
    BinaryStream failed{0, output, 1};
    CHECK(!failed.record(1,
                         [](BinaryWriter& out) noexcept
                         {
                             return out.fail();
                         }));
    CHECK(failed.result().status == Status::InvalidData);
    BinaryStream zero{0, output, 1};
    CHECK(!zero.atomic(0,
                       [](resource::Output) noexcept
                       {
                       }));
    CHECK(zero.result().status == Status::InvalidData);
    std::printf("Binary stream: %u checks\n", checks);
}
