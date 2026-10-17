/* Offline retained roots for every optional mailbox operation. MIT. */
#include "Fixture.hpp"
using namespace rpc_fixture;
#if defined(ARM_PROBE) || defined(RPC_ARM_PROBE)
extern "C" [[noreturn]] void abort()
{
	__builtin_trap();
}
#endif
extern "C" std::uint32_t rpc_write(rpc::RpcMailbox& mailbox, const std::byte* data,
                                   std::uint32_t size, std::uint64_t cursorValue,
                                   bool final) noexcept
{
	const auto result = mailbox.write(cursorValue, {data, size}, final);
	return result.consumed + static_cast<unsigned>(result.status) +
	       static_cast<unsigned>(result.complete);
}

extern "C" std::uint32_t rpc_read(const rpc::RpcMailbox& mailbox, std::byte* data,
                                  std::uint32_t size, std::uint64_t cursorValue) noexcept
{
	const auto result = mailbox.read(cursorValue, {data, size});
	return result.written + static_cast<unsigned>(result.status) +
	       static_cast<unsigned>(result.eof);
}

extern "C" void rpc_process(rpc::RpcMailbox& mailbox, std::uint32_t now) noexcept
{
	mailbox.process(now);
}

extern "C" void rpc_reset(rpc::RpcMailbox& mailbox) noexcept
{
	mailbox.resetSession();
}

extern "C" std::uint32_t rpc_size(const rpc::RpcMailbox& mailbox) noexcept
{
	return mailbox.size();
}

static std::array<std::byte, 24 + ts::wireSize<Big>> request;
static std::array<std::byte, ts::wireSize<Big>> result;
static std::array<std::byte, model.maxScratch()> scratch;
static ts::Workspace workspace{scratch};
static rpc::RpcMailbox mailbox{view, workspace, request, result};
static std::array<std::byte, 64> transport;

extern "C" std::uint32_t rpc_roots(unsigned operation) noexcept
{
	if (operation == 0)
		return rpc_write(mailbox, transport.data(), 24, 0, true);
	if (operation == 1)
		return rpc_read(mailbox, transport.data(), transport.size(), 0);
	if (operation == 2) {
		rpc_process(mailbox, 1);
		return rpc_size(mailbox);
	}
	rpc_reset(mailbox);
	return rpc_size(mailbox);
}
#ifdef RPC_FRAME_CONTROL
extern "C" __attribute__((noinline)) unsigned rpc_frame_control(unsigned value) noexcept
{
	volatile std::byte oversizedFrame[4096];
	for (unsigned i = 0; i < 4096; ++i)
		oversizedFrame[i] = std::byte(value + i);
	return std::to_integer<unsigned>(oversizedFrame[value & 4095]);
}
#endif
#ifdef ABI_MAIN
static volatile unsigned selectedOperation = 2;

int main()
{
	return static_cast<int>(rpc_roots(selectedOperation));
}
#endif
