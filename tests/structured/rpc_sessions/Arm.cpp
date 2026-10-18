/* Real retained pool API roots and a bounded offline frame control. MIT. */
#include "Fixture.hpp"
using namespace rpc_sessions_fixture;

#ifdef RPC_SESSIONS_ARM_PROBE
extern "C" [[noreturn]] void abort()
{
	__builtin_trap();
}
#endif

using Pool = rpc::RpcSessionPool<2>;

extern "C" unsigned rpc_sessions_acquire(Pool& pool, rpc::RpcSessionHandle& output,
                                         std::uint32_t peer, std::uint32_t generation,
                                         std::uint32_t channel) noexcept
{
	const auto result = pool.acquire({peer, generation, channel});
	output = result.session;
	return static_cast<unsigned>(result.status);
}

extern "C" unsigned rpc_sessions_handle(Pool& pool, rpc::RpcSessionHandle session,
                                        const std::byte* data, std::uint32_t size, std::byte* reply,
                                        std::uint32_t capacity,
                                        rpc::RpcReplyTicket& ticket) noexcept
{
	const auto result = pool.handle(session, {data, size}, {reply, capacity});
	ticket = result.ticket;
	return static_cast<unsigned>(result.status) + static_cast<unsigned>(result.reply.status) +
	       result.reply.written;
}

extern "C" unsigned rpc_sessions_reply_accepted(Pool& pool, rpc::RpcSessionHandle session,
                                                rpc::RpcReplyTicket ticket) noexcept
{
	return static_cast<unsigned>(pool.replyAccepted(session, ticket));
}

extern "C" unsigned rpc_sessions_process(Pool& pool, std::uint32_t now,
                                         std::uint32_t budget) noexcept
{
	const auto result = pool.process(now, budget);
	return static_cast<unsigned>(result.status) + result.dispatched + result.visited;
}

extern "C" unsigned rpc_sessions_release(Pool& pool, rpc::RpcSessionHandle session) noexcept
{
	return static_cast<unsigned>(pool.release(session)) + pool.valid(session);
}

static std::array<std::byte, 128> scratch;
static ts::Workspace workspace{scratch};
static std::array<std::byte, 64> firstRequest, secondRequest;
static std::array<std::byte, 32> firstResult, secondResult;
static rpc::RpcMailbox firstMailbox{firstView, workspace, firstRequest, firstResult};
static rpc::RpcMailbox secondMailbox{firstView, workspace, secondRequest, secondResult};
// Literal path validation is constant evaluation, as in an application's
// fixed provider tables. It must not retain library range-error formatting.
static constinit const auto firstFiles =
    resource::filesystem(resource::file("/telemetry/rpc.bin", firstMailbox));
static constinit const auto secondFiles =
    resource::filesystem(resource::file("/telemetry/rpc.bin", secondMailbox));
static Pool pool{std::array{rpc::RpcSessionBinding{firstMailbox, firstFiles.view()},
                            rpc::RpcSessionBinding{secondMailbox, secondFiles.view()}}};
static rpc::RpcSessionHandle active;
static rpc::RpcReplyTicket ticket;
static std::array<std::byte, 128> packet{}, reply{};

extern "C" unsigned rpc_sessions_roots(unsigned operation) noexcept
{
	if (operation == 0)
		return rpc_sessions_acquire(pool, active, 1, 1, 0);
	if (operation == 1)
		return rpc_sessions_handle(pool, active, packet.data(), packet.size(), reply.data(),
		                           reply.size(), ticket);
	if (operation == 2)
		return rpc_sessions_reply_accepted(pool, active, ticket);
	if (operation == 3)
		return rpc_sessions_process(pool, 100, 1);
	return rpc_sessions_release(pool, active);
}

#ifdef RPC_SESSIONS_FRAME_CONTROL
extern "C" __attribute__((noinline)) unsigned rpc_sessions_frame_control(unsigned value) noexcept
{
	volatile std::byte oversized[4096];
	for (unsigned i = 0; i < 4096; ++i)
		oversized[i] = std::byte(value + i);
	return std::to_integer<unsigned>(oversized[value & 4095]);
}
#endif

#ifdef ABI_MAIN
static volatile unsigned selectedOperation = 3;

int main()
{
	return static_cast<int>(rpc_sessions_roots(selectedOperation));
}
#endif
