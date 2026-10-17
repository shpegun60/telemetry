/* Explicit provider composition, with no init() lifecycle. MIT. */
#include "DeviceResources.hpp"
#ifdef WITH_V3
#include "Fixture.hpp"
#endif
#ifdef WITH_RPC
#include <resource/telemetry/v3/RpcMailbox.hpp>
#include <telemetry/model/Model.hpp>
#endif

namespace {
// Independent generic byte provider proves the core-only build needs no telemetry types.
// API: size(), read().
struct Plain {
	constexpr resource::FileSize size() const noexcept
	{
		return 0;
	}

	resource::ReadResult read(resource::Cursor c, resource::Output) const noexcept
	{
		return {c == 0 ? resource::Status::Ok : resource::Status::InvalidCursor, c, 0, c == 0};
	}
};

constexpr Plain plain;
#ifdef WITH_RPC
// Deliberately independent of Fixture.hpp: RPC-only must not need Values.cpp.
std::uint32_t rpcReads = 41;

std::uint32_t readRpcCounter() noexcept
{
	return ++rpcReads;
}

constexpr telemetry::FieldTable rpcLocal{telemetry::field<&readRpcCounter>("Counter")};
constexpr telemetry::FieldCatalogTable rpcFields{telemetry::group("rpc", rpcLocal)};
constexpr telemetry::ServiceCatalogTable<> rpcServices{};
constexpr telemetry::Model rpcModel{rpcFields, telemetry::emptyCommands, rpcServices};
constexpr auto rpcView = rpcModel.view();
std::array<std::byte, rpcModel.maxScratch()> rpcScratch{};
telemetry::Workspace rpcWorkspace{rpcScratch};
std::array<std::byte, 24> rpcRequest{};
std::array<std::byte, 4> rpcResult{};
resource::telemetry::v3::RpcMailbox rpcMailbox{rpcView, rpcWorkspace, rpcRequest, rpcResult};
#endif
constinit auto files =
    resource::filesystem(resource::file("/plain", plain)
#ifdef WITH_V3
                             ,
                         resource::file("/v3/descriptor.bin", fixture::packedFile),
                         resource::file("/v3/values.bin", fixture::values)
#endif
#ifdef WITH_RPC
                             ,
                         resource::file("/telemetry/rpc.bin", rpcMailbox)
#endif
    );
} // namespace

resource::FileSystemView deviceResources() noexcept
{
	return files.view();
}

void processDeviceResources(std::uint32_t nowMs) noexcept
{
#ifdef WITH_RPC
	rpcMailbox.process(nowMs);
#else
	(void)nowMs;
#endif
}
