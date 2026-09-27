/* Model views and per-peer agreement state must have explicit lifetimes. MIT. */
#include "Fixture.hpp"
using namespace fixture;
struct Conversion { operator ts::ModelView() const { return model.view(); } };
void reject()
{
    rs::Binding peer;
    std::array<std::byte, 8> response{};
#if CASE == 1
    (void)rs::Bind::process(peer, model.view(), descriptor.fingerprint(), handshake(), response);
#elif CASE == 2
    (void)rs::Bind::process(peer, {model.view()}, descriptor.fingerprint(), handshake(), response);
#elif CASE == 3
    Conversion conversion;
    (void)rs::Bind::process(peer, conversion, descriptor.fingerprint(), handshake(), response);
#elif CASE == 4
    (void)rs::Bind::process<const ts::ModelView&>(peer, model.view(), descriptor.fingerprint(), handshake(), response);
#elif CASE == 5
    (void)rs::Bind::process<const ts::ModelView&>(peer, {model.view()}, descriptor.fingerprint(), handshake(), response);
#elif CASE == 6
    (void)rs::Bind::process(peer, std::move(view), descriptor.fingerprint(), handshake(), response);
#elif CASE == 7
    auto copied = peer; (void)copied;
#elif CASE == 8
    auto moved = std::move(peer); (void)moved;
#elif CASE == 9
    rs::Binding other; other = peer;
#elif CASE == 10
    rs::Binding other; other = std::move(peer);
#else
    (void)rs::Bind::process(peer, view, descriptor.fingerprint(), handshake(), response);
#endif
    (void)response;
}
