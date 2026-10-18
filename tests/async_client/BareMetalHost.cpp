/* Host board port for the unchanged bare-metal client loop. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#include "../codegen/Device.hpp"
#include <client/ClientResult.hpp>
#include <integration/encoded/Dispatcher.hpp>
#include <algorithm>

namespace cl = telemetry::client;
static std::array<std::byte, 1024> reply{};
static std::size_t replyBytes = 0;
static std::uint32_t clockMs = 0, peerSession = 0, peerRequest = 0;

void board_init() noexcept
{}

std::uint32_t board_millis() noexcept
{
	return clockMs;
}

cl::Input board_verifiedDescriptorHeader() noexcept
{
	return cl::Input{codegen_fixture::descriptorBytes}.first(64);
}

bool board_running() noexcept
{
	return clockMs < 1000;
}

void board_poll() noexcept
{
	++clockMs;
}

bool board_txAvailable(std::size_t) noexcept
{
	return replyBytes == 0;
}

bool board_beginPacket(cl::Input bytes, std::uint32_t session, std::uint32_t request) noexcept
{
	const auto result = telemetry::integration::encoded::dispatchEncoded(
	    codegen_fixture::model.view(), codegen_fixture::workspace, session, bytes, reply);
	if (!result)
		return false;
	replyBytes = result.written;
	peerSession = session;
	peerRequest = request;
	return true;
}

bool board_pollReply(cl::Output storage, std::size_t& bytes, std::uint32_t& session,
                     std::uint32_t& request) noexcept
{
	if (!replyBytes || storage.size() < replyBytes)
		return false;
	std::copy_n(reply.begin(), replyBytes, storage.begin());
	bytes = std::exchange(replyBytes, 0);
	session = peerSession;
	request = peerRequest;
	return true;
}

void board_closeSession() noexcept
{}
