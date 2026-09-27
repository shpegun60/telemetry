/* Explicit provider composition, with no init() lifecycle. MIT. */
#include "DeviceResources.hpp"
#ifdef WITH_V3
#include "Fixture.hpp"
#endif
#ifdef WITH_V2
#include <resource/telemetry/SchemaFile.hpp>
#endif

namespace {
struct Plain {
    constexpr resource::FileSize size() const noexcept { return 0; }
    resource::ReadResult read(resource::Cursor c, resource::Output) const noexcept
    { return {c == 0 ? resource::Status::Ok : resource::Status::InvalidCursor, c, 0, c == 0}; }
};
constexpr Plain plain;
#ifdef WITH_V2
const telemetry_resource::SchemaFile schema{telemetry::CatalogIndex{nullptr, 0}};
#endif
constinit auto files = resource::filesystem(
    resource::file("/plain", plain)
#ifdef WITH_V2
    , resource::file("/v2/schema.bin", schema)
#endif
#ifdef WITH_V3
    , resource::file("/v3/descriptor.bin", fixture::packedFile)
    , resource::file("/v3/values.bin", fixture::values)
#endif
);
}

resource::FileSystemView deviceResources() noexcept { return files.view(); }
