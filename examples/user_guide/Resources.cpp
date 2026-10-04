/*
 * @file Resources.cpp
 * @brief Flat files, partial writes and complete resource packets.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <resource/Resource.hpp>
#include <resource/protocol/Protocol.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace guide
{
using namespace resource;

// Application policy: RAM storage and at most two consumed bytes per call.
// final acknowledges the submitted stream; this example does not persist RAM.
class SettingsFile
{
public:
    FileSize size() const noexcept { ++stats; return static_cast<FileSize>(bytes_.size()); }

    ReadResult read(Cursor cursor, Output output) const noexcept
    {
        return BytesFile{bytes_}.read(cursor, output);
    }

    WriteResult write(Cursor cursor, Input input, bool final) noexcept
    {
        if (cursor > size()) return {Status::InvalidCursor, cursor};
        const auto remaining = size() - static_cast<FileSize>(cursor);
        if (input.size() > remaining) return {Status::InvalidData, cursor};
        if (input.empty())
            return final ? WriteResult{Status::Ok, cursor, 0, true}
                         : WriteResult{Status::BufferTooSmall, cursor};

        const auto consumed = static_cast<FileSize>(std::min(input.size(), std::size_t{2}));
        std::memmove(bytes_.data() + static_cast<FileSize>(cursor), input.data(), consumed);
        ++writes;
        return {Status::Ok, cursor + consumed, consumed, final && consumed == input.size()};
    }

    unsigned writes = 0;
    mutable unsigned stats = 0;

private:
    std::array<std::byte, 8> bytes_{};
};

inline constexpr std::array versionBytes{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
inline constexpr BytesFile versionFile{versionBytes};
inline constexpr char jsonText[] = "{\"ready\":true}";
inline const BytesFile jsonFile{std::as_bytes(std::span{jsonText}).first(sizeof(jsonText) - 1)};
inline SettingsFile settings;
inline constexpr FileIndex Version = 0, Settings = 1, Json = 2;
inline constinit const auto files = filesystem(
    file("/device/version.bin", versionFile), file("/settings.bin", settings),
    file("/status.json", jsonFile));

// Only byte helpers for this example. They do not implement transport framing.
template <class U>
void put(Output bytes, std::size_t offset, U value) noexcept
{
    for (std::size_t i = 0; i < sizeof(U); ++i)
        bytes[offset + i] = static_cast<std::byte>(value >> (8 * i));
}

template <class U>
U get(Input bytes, std::size_t offset) noexcept
{
    U value = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i)
        value |= U{std::to_integer<unsigned char>(bytes[offset + i])} << (8 * i);
    return value;
}

template <std::size_t N>
std::array<std::byte, 16 + N> writePacket(Cursor cursor,
                                        const std::array<std::byte, N>& data) noexcept
{
    static_assert(N <= UINT16_MAX);
    std::array<std::byte, 16 + N> packet{};
    packet[0] = static_cast<std::byte>(protocol::Op::Write);
    put(packet, 1, Settings);
    put(packet, 5, cursor);
    packet[13] = std::byte{1};
    put(packet, 14, static_cast<std::uint16_t>(N));
    std::copy(data.begin(), data.end(), packet.begin() + 16);
    return packet;
}
} // namespace guide

int main()
{
    using namespace guide;
    unsigned checks = 0;
    const auto check = [&checks](bool ok) {
        ++checks;
        if (!ok) std::abort();
    };

    const auto view = files.view(); // Borrows the stable table; no provider copy.
    check(view.fileCount() == 3 && view.path(Json) == "/status.json");
    check(files.size() == 3 && !files.empty() && view.size() == files.size());
    const auto statsBefore = settings.stats;
    FileIndex nextIndex = 0;
    for (const auto entry : files)
    {
        check(entry.valid() && entry.index() == nextIndex && entry.path() == view.path(nextIndex));
        check(entry.readable() && entry.writable() == (nextIndex == Settings));
        ++nextIndex;
    }
    check(nextIndex == files.size() && settings.stats == statsBefore && settings.writes == 0);
    for (const auto entry : files.view()) // A temporary view still borrows the static table.
        check(entry.path() == files.path(entry.index()));
    check(settings.stats == statsBefore); // Enumeration did not call size()/stat().
    const auto settingsEntry = view[Settings];
    check(settingsEntry && settingsEntry.index() == Settings && settings.stats == statsBefore);
    check(settingsEntry.stat().status == Status::Ok && settings.stats == statsBefore + 1);
    const auto absent = view[99];
    check(!absent && absent.index() == 99 && absent.path().empty());
    check(!absent.readable() && !absent.writable());
    check(absent.stat().status == Status::InvalidFile);
    const auto absentRead = absent.read(7, {});
    check(absentRead.status == Status::InvalidFile && absentRead.next == 7 && absentRead.written == 0);
    check(absent.write(9, {}).status == Status::InvalidFile);
    check(view.stat(Version).flags == FileFlag::Readable);
    check(view.stat(Settings).flags == (FileFlag::Readable | FileFlag::Writable));
    check(view.write(Version, 0, {}).status == Status::NotWritable);

    // Direct local writes: advance by consumed, then resubmit the suffix with
    // the returned cursor and the same final indication.
    const std::array data{std::byte{10}, std::byte{20}, std::byte{30}};
    auto pending = Input{data};
    Cursor cursor = 0;
    while (!pending.empty())
    {
        const auto result = view.write(Settings, cursor, pending, true);
        check(result.status == Status::Ok && result.consumed > 0 && result.consumed <= pending.size());
        pending = pending.subspan(result.consumed);
        cursor = result.next;
        check(result.complete == pending.empty());
    }
    std::array<std::byte, 3> output{};
    const auto localRead = view.read(Settings, 0, output);
    check(localRead.status == Status::Ok && localRead.written == output.size() && output == data);
    check(view.write(Settings, 3, {}, true).complete);
    check(view.write(Settings, 3, {}, false).status == Status::BufferTooSmall);

    std::array<std::byte, 128> response{};
    std::array<std::byte, 9> list{};
    list[0] = static_cast<std::byte>(protocol::Op::List); // u64 cursor is zero.
    auto reply = protocol::process(view, list, response);
    check(reply.status == Status::Ok && response[9] == std::byte{1});
    check(get<Cursor>(response, 1) == 3 && get<std::uint16_t>(response, 10) == reply.written - 12);
    const auto firstPathSize = get<std::uint16_t>(response, 12);
    check(firstPathSize == view.path(Version).size() &&
          std::memcmp(response.data() + 14, view.path(Version).data(), firstPathSize) == 0);

    std::array<std::byte, 5> stat{};
    stat[0] = static_cast<std::byte>(protocol::Op::Stat);
    put(stat, 1, Settings);
    reply = protocol::process(view, stat, response);
    check(reply.status == Status::Ok && reply.written == 6 && get<FileSize>(response, 1) == settings.size());
    check(response[5] == static_cast<std::byte>(FileFlag::Readable | FileFlag::Writable));

    std::array<std::byte, 13> read{};
    read[0] = static_cast<std::byte>(protocol::Op::Read);
    put(read, 1, Version);
    reply = protocol::process(view, read, response);
    check(reply.status == Status::Ok && reply.written == 16 && response[9] == std::byte{1});
    check(get<Cursor>(response, 1) == 4 && get<std::uint16_t>(response, 10) == 4);
    check(std::equal(versionBytes.begin(), versionBytes.end(), response.begin() + 12));

    // Simulate two input fragments of a packet whose complete length is known
    // by this harness. The real UART/TCP layer must determine that boundary.
    const auto packet = writePacket(3, data);
    std::array<std::byte, packet.size()> assembled{};
    const auto writesBefore = settings.writes;
    std::copy(packet.begin(), packet.begin() + 7, assembled.begin());
    check(settings.writes == writesBefore); // No process call on the fragment.
    std::copy(packet.begin() + 7, packet.end(), assembled.begin() + 7);
    reply = protocol::process(view, assembled, response);
    check(reply.status == Status::Ok && reply.written == 14);
    check(get<Cursor>(response, 1) == 5 && get<FileSize>(response, 9) == 2 && response[13] == std::byte{0});

    const std::array suffix{data.back()};
    const auto remainingPacket = writePacket(5, suffix);
    reply = protocol::process(view, remainingPacket, response);
    check(reply.status == Status::Ok && get<Cursor>(response, 1) == 6 &&
          get<FileSize>(response, 9) == 1 && response[13] == std::byte{1});
    check(settings.writes == writesBefore + 2);

    // Complete-but-malformed input and short response capacity cannot write RAM.
    auto malformed = remainingPacket;
    malformed[13] = std::byte{2}; // final is a canonical bool, only 0 or 1.
    const auto writesAfter = settings.writes;
    reply = protocol::process(view, malformed, response);
    check(reply.status == Status::InvalidData && reply.written == 1 && settings.writes == writesAfter);
    reply = protocol::process(view, remainingPacket, Output{response}.first(13));
    check(reply.status == Status::BufferTooSmall && reply.written == 0 && settings.writes == writesAfter);

    std::printf("Resource guide: %u checks passed\n", checks);
}
