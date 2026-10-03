/* Generic resource layout and constant-provider dispatch, without telemetry.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <cstddef>

struct ProbeProvider {
    resource::FileSize size() const noexcept { return 1; }
    resource::ReadResult read(resource::Cursor cursor, resource::Output out) const noexcept
    {
        if (cursor > 1) return {resource::Status::InvalidCursor, cursor};
        if (cursor == 1) return {resource::Status::Ok, cursor, 0, true};
        if (out.empty()) return {resource::Status::BufferTooSmall, cursor};
        out[0] = std::byte{42};
        return {resource::Status::Ok, 1, 1, true};
    }
} resource_probe_provider;

extern constexpr auto resource_probe_files =
    resource::filesystem(resource::file("/probe", resource_probe_provider));
static_assert(sizeof(resource::FileEntry) == 16 && alignof(resource::FileEntry) == 4);
static_assert(offsetof(resource::FileEntry, path) == 0 &&
              offsetof(resource::FileEntry, object) == 8 && offsetof(resource::FileEntry, ops) == 12);
static_assert(sizeof(resource::FileSystemView) == 8);
static_assert(sizeof(resource::ReadResult) == 16 && alignof(resource::ReadResult) == 8);
static_assert(offsetof(resource::ReadResult, next) == 0 && offsetof(resource::ReadResult, written) == 8 &&
              offsetof(resource::ReadResult, status) == 12 && offsetof(resource::ReadResult, eof) == 13);
static_assert(sizeof(resource::WriteResult) == 16 && alignof(resource::WriteResult) == 8);
static_assert(offsetof(resource::WriteResult, next) == 0 && offsetof(resource::WriteResult, consumed) == 8 &&
              offsetof(resource::WriteResult, status) == 12 && offsetof(resource::WriteResult, complete) == 13);
static_assert(sizeof(resource::FileStat) == 8 && alignof(resource::FileStat) == 4);
static_assert(offsetof(resource::FileStat, size) == 0 && offsetof(resource::FileStat, status) == 4 &&
              offsetof(resource::FileStat, flags) == 5);

extern "C" resource::ReadResult resource_probe_read(resource::FileSystemView view,
    resource::FileIndex index, resource::Cursor cursor, std::byte* out, std::size_t size) noexcept
{ return view.read(index, cursor, {out, size}); }

extern "C" resource::ReadResult resource_probe_known(std::byte* out, std::size_t size) noexcept
{ return resource_probe_files.read(0, 0, {out, size}); }

extern "C" resource::FileStat resource_probe_stat(resource::FileSystemView view,
                                                 resource::FileIndex index) noexcept
{ return view.stat(index); }

int main()
{
    const std::byte request[9]{std::byte{1}};
    std::byte reply[64];
    return resource_protocol::process(resource_probe_files.view(), request, reply).status ==
           resource::Status::Ok ? 0 : 1;
}
