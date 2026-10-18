/* One executable for each independent qmake dependency selection. MIT. */
#include "DeviceResources.hpp"
#include <cassert>
#include <array>
#ifdef WITH_V3
#include "Fixture.hpp"
#endif
int main()
{
	const auto files = deviceResources();
	unsigned position = 0;
	assert(files.stat(position++).size == 0);
	std::array<std::byte, 4096> bytes{};
#ifdef WITH_V3
	assert(files.read(position++, 0, bytes).eof && bytes[4] == std::byte{3});
	assert(files.read(position++, 0, bytes).eof && bytes[0] == std::byte{'T'} &&
	       bytes[1] == std::byte{'V'});
#endif
#ifdef WITH_RPC
	const auto rpcIndex = position++;
	// Independent explicit wire bytes; the facade exposes only generic resources.
	std::array<std::byte, 24> request{std::byte{'T'}, std::byte{'R'}, std::byte{'P'},
	                                  std::byte{'C'}, std::byte{1}};
	request[8] = std::byte{1};  // transactionId
	request[20] = std::byte{4}; // FieldRead
	const auto write = files.write(rpcIndex, 0, request, true);
	assert(write.status == resource::Status::Ok && write.consumed == 24 && write.complete);
	assert(files.read(rpcIndex, 0, bytes).eof && bytes[21] == std::byte{2}); // Pending
	processDeviceResources(1);
	assert(files.stat(rpcIndex).size == 36);
	const auto ready = files.read(rpcIndex, 0, bytes);
	assert(ready.status == resource::Status::Ok && ready.written == 36 && ready.eof);
	assert(bytes[21] == std::byte{4} && bytes[22] == std::byte{0} && bytes[32] == std::byte{42});
	const auto retry = files.write(rpcIndex, 0, request, true);
	assert(retry.complete && retry.status == resource::Status::Ok);
	processDeviceResources(2);
	assert(files.read(rpcIndex, 0, bytes).eof && bytes[32] == std::byte{42});
	request[8] = std::byte{2};
	assert(files.write(rpcIndex, 0, request, true).complete);
	processDeviceResources(3);
	assert(files.read(rpcIndex, 0, bytes).eof && bytes[32] == std::byte{43});
#endif
#ifdef WITH_RPC_CLIENT
	assert(exerciseDeviceRpcClient());
#endif
#ifdef WITH_RPC_SESSIONS
	assert(exerciseDeviceRpcSessions());
#endif
	assert(position == files.fileCount());
	(void)bytes;
}
