/* Session ownership cannot be copied, moved or bound to a temporary mailbox. MIT. */
#include "Fixture.hpp"

using namespace rpc_sessions_fixture;

void reject()
{
	Harness<1> h;
	const std::array bindings{rpc::RpcSessionBinding{h.a.mailbox, h.a.files.view()}};
#if defined(RPC_SESSIONS_NEG_COPY)
	auto invalid = h.pool;
	(void)invalid;
#elif defined(RPC_SESSIONS_NEG_MOVE)
	auto invalid = std::move(h.pool);
	(void)invalid;
#elif defined(RPC_SESSIONS_NEG_COPY_ASSIGN)
	Harness<1> other;
	other.pool = h.pool;
#elif defined(RPC_SESSIONS_NEG_MOVE_ASSIGN)
	Harness<1> other;
	other.pool = std::move(h.pool);
#elif defined(RPC_SESSIONS_NEG_WRONG_CAPACITY)
	const std::array two{bindings[0], bindings[0]};
	rpc::RpcSessionPool<1> invalid{two};
	(void)invalid;
#elif defined(RPC_SESSIONS_NEG_WRONG_BINDING)
	rpc::RpcSessionPool<1> invalid{std::array<int, 1>{0}};
	(void)invalid;
#elif defined(RPC_SESSIONS_NEG_TEMPORARY_MAILBOX)
	std::array<std::byte, 64> request{}, result{};
	rpc::RpcSessionBinding invalid{rpc::RpcMailbox{firstView, h.workspace, request, result},
	                               h.a.files.view()};
	(void)invalid;
#elif defined(RPC_SESSIONS_NEG_ZERO_CAPACITY)
	rpc::RpcSessionPool<0> invalid{std::array<rpc::RpcSessionBinding, 0>{}};
	(void)invalid;
#endif
	(void)bindings;
}
