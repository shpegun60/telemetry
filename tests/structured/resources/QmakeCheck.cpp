/* One executable for each independent qmake dependency selection. MIT. */
#include "DeviceResources.hpp"
#include <cassert>
#include <array>
#ifdef WITH_V3
#include <resource/structured/Bind.hpp>
#include <resource/structured/Exchange.hpp>
#include "Fixture.hpp"
#endif
int main()
{
    const auto files = deviceResources();
    unsigned position = 0;
    assert(files.stat(position++).size == 0);
    std::array<std::byte, 4096> bytes{};
#ifdef WITH_V2
    assert(files.read(position++, 0, bytes).status == resource::Status::Ok);
    assert(bytes[4] == std::byte{2} && bytes[6] == std::byte{1});
#endif
#ifdef WITH_V3
    assert(files.read(position++, 0, bytes).eof && bytes[4] == std::byte{3});
    assert(files.read(position++, 0, bytes).eof && bytes[0] == std::byte{'T'} && bytes[1] == std::byte{'V'});
    resource::structured::Binding peer;
    // Refer to both optional compiled handlers: missing .pri sources must fail
    // to link even when the example does not carry a transport implementation.
    const auto view = fixture::model.view();
    assert(resource::structured::Bind::process(peer, view, fixture::descriptor.fingerprint(), {}, bytes).written == 8);
    assert(resource::structured::Exchange::process(peer, {}, bytes, fixture::workspace).written == 0);
    peer.reset();
#endif
    assert(position == files.fileCount());
    (void)bytes;
}
