/* Borrowed mailbox context requires explicit lifetimes; live state cannot move. MIT. */
#include "Fixture.hpp"
#include <utility>
using namespace rpc_fixture;

struct Conversion {
	operator ts::ModelView() const noexcept
	{
		return model.view();
	}
};

void reject()
{
	std::array<std::byte, 64> request{}, result{}, scratch{};
	ts::Workspace workspace{scratch};
	rpc::RpcMailbox mailbox{view, workspace, request, result};
#if defined(RPC_NEG_COPY)
	auto copied = mailbox;
	(void)copied;
#elif defined(RPC_NEG_MOVE)
	auto moved = std::move(mailbox);
	(void)moved;
#elif defined(RPC_NEG_MODEL_TEMPORARY)
	rpc::RpcMailbox invalid{model.view(), workspace, request, result};
	(void)invalid;
#elif defined(RPC_NEG_WORKSPACE_TEMPORARY)
	rpc::RpcMailbox invalid{view, ts::Workspace{scratch}, request, result};
	(void)invalid;
#elif CASE == 5
	rpc::RpcMailbox invalid{{model.view()}, workspace, request, result};
	(void)invalid;
#elif CASE == 6
	Conversion conversion;
	rpc::RpcMailbox invalid{conversion, workspace, request, result};
	(void)invalid;
#elif CASE == 7
	rpc::RpcMailbox other{view, workspace, request, result};
	other = mailbox;
#elif CASE == 8
	rpc::RpcMailbox other{view, workspace, request, result};
	other = std::move(mailbox);
#endif
	(void)mailbox;
}
