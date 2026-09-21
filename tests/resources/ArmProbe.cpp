// Cortex-M7 layout, static storage and runtime dispatch probes (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include <resource/FileSystem.hpp>
#include "../../app/resources/DeviceResources.hpp"
#include <cstddef>

struct ProbeProvider
{
    resource::FileSize size() const noexcept
    {
        return 1;
    }

    resource::ReadResult read(resource::Cursor c, resource::Output out) const noexcept
    {
        if (c > 1)
        {
            return {resource::Status::InvalidCursor, c};
        }
        if (c == 1)
        {
            return {resource::Status::Ok, c, 0, true};
        }
        if (out.empty())
        {
            return {resource::Status::BufferTooSmall, c};
        }
        out[0] = std::byte{42};
        return {resource::Status::Ok, 1, 1, true};
    }
} resource_probe_provider;

extern constexpr auto resource_probe_files =
    resource::filesystem(resource::file("/probe", resource_probe_provider));
static_assert(sizeof(resource::FileEntry) == 16 && alignof(resource::FileEntry) == 4);
static_assert(offsetof(resource::FileEntry, path) == 0 &&
              offsetof(resource::FileEntry, object) == 8 &&
              offsetof(resource::FileEntry, ops) == 12);
static_assert(sizeof(resource::FileSystemView) == 8);

extern "C" resource::ReadResult resource_probe_read(resource::FileSystemView view,
                                                    resource::FileIndex i, resource::Cursor c,
                                                    std::byte* out, std::size_t n) noexcept
{
    return view.read(i, c, {out, n});
}

extern "C" resource::ReadResult resource_probe_known(std::byte* out, std::size_t n) noexcept
{
    return resource_probe_files.read(0, 0, {out, n});
}

extern "C" resource::FileStat resource_probe_stat(resource::FileSystemView view,
                                                  resource::FileIndex i) noexcept
{
    return view.stat(i);
}

int main()
{
    std::byte bytes[64];
    const auto r = device::resources::read(0, 0, bytes);
    return r.status == resource::Status::Ok ? 0 : 1;
}
