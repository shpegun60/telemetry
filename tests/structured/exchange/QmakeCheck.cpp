/* Explicit example linkage: neither library .pri selects this protocol. MIT. */
#include "Fixture.hpp"
using namespace fixture;

int main()
{
	rs::Binding peer;
	std::array<std::byte, 32> response{};
	assert(
	    rs::Bind::process(peer, view, descriptor.fingerprint(), handshake(), response).dispatch ==
	    D::Ok);
	const auto request = emptyPacket(Op::Command, 1);
	ts::Workspace workspace{{}};
	assert(rs::Exchange::process(peer, request, response, workspace).dispatch == D::Ok);
	assert(device.commands == 1);
}
