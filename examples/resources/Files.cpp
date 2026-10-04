/*
 * @file Files.cpp
 * @brief Three flat files: borrowed immutable bytes and a writable RAM provider.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include <resource/Resource.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
using namespace resource;

// This example stores bytes in RAM. A real provider may instead call a logger,
// SD driver or application serializer. The path never selects the backend.
class SettingsFile
{
public:
    constexpr FileSize size() const noexcept
    {
        return static_cast<FileSize>(bytes_.size());
    }

    ReadResult read(Cursor cursor, Output output) const noexcept
    {
        return BytesFile{bytes_}.read(cursor, output);
    }

    WriteResult write(Cursor cursor, Input input, bool final) noexcept
    {
        // This provider accepts the entire submitted chunk or none of it.
        // The generic API also permits providers that consume only a prefix.
        if (cursor > size())
        {
            return {Status::InvalidCursor, cursor};
        }
        if (input.size() > size() - static_cast<FileSize>(cursor))
        {
            return {Status::InvalidData, cursor};
        }
        if (input.empty() && !final)
        {
            return {Status::BufferTooSmall, cursor};
        }
        if (!input.empty())
        {
            std::memmove(bytes_.data() + cursor, input.data(), input.size());
        }
        return {Status::Ok, cursor + input.size(), static_cast<FileSize>(input.size()), final};
    }

private:
    std::array<std::byte, 16> bytes_{};
};

inline constexpr std::array versionBytes{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
inline constexpr std::array logHeaderBytes{std::byte{'L'}, std::byte{'O'}, std::byte{'G'},
                                           std::byte{1}};
inline constexpr BytesFile version{versionBytes};
inline constexpr BytesFile logHeader{logHeaderBytes};
inline SettingsFile settings;

// Identity is the declaration position. Named constants avoid magic numbers
// in application calls; no second identity is stored in each descriptor.
inline constexpr FileIndex Version = 0;
inline constexpr FileIndex Settings = 1;
inline constexpr FileIndex LogHeader = 2;
inline constinit const auto files =
    filesystem(file("/device/version.bin", version), file("/user/settings.bin", settings),
               file("/logs/header.bin", logHeader));
static_assert(files.fileCount() == 3);
} // namespace

int main()
{
    unsigned checks = 0;
    const auto check = [&checks](bool ok)
    {
        ++checks;
        if (!ok)
        {
            std::abort();
        }
    };

    check(files.path(Version) == "/device/version.bin");
    check(files.path(Settings) == "/user/settings.bin");
    check(files.path(LogHeader) == "/logs/header.bin");
    check(files.stat(Settings).flags == (FileFlag::Readable | FileFlag::Writable));
    check(files.write(Version, 0, {}).status == Status::NotWritable);
    check(files.path(3).empty());
    check(files.stat(3).status == Status::InvalidFile);

    // Local byte access. Stop on error or EOF; always consume only `written`.
    std::array<std::byte, 3> output{};
    Cursor cursor = 0;
    std::array<std::byte, 4> reconstructed{};
    std::size_t used = 0;
    do
    {
        const auto result = files.read(Version, cursor, output);
        check(result.status == Status::Ok);
        check(result.written <= output.size());
        check(used + result.written <= reconstructed.size());
        std::memcpy(reconstructed.data() + used, output.data(), result.written);
        used += result.written;
        cursor = result.next;
        if (result.eof)
        {
            break;
        }
        check(result.written != 0);
    } while (true);
    check(reconstructed == versionBytes);

    const std::array input{std::byte{0x12}, std::byte{0x34}, std::byte{0x56}};
    const auto written = files.write(Settings, 5, input, true);
    check(written.status == Status::Ok && written.next == 8);
    check(written.consumed == input.size() && written.complete);
    const auto read = files.read(Settings, 5, output);
    check(read.status == Status::Ok && read.written == input.size());
    check(output == input);
    check(files.write(Settings, 17, input).status == Status::InvalidCursor);
    check(files.write(Settings, 15, input).status == Status::InvalidData);
    check(files.write(Settings, 8, {}, false).status == Status::BufferTooSmall);
    check(files.write(Settings, 8, {}, true).complete);

    std::printf("Resource example: %u checks passed\n", checks);
}
