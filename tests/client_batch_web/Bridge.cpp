/* Persistent native Model/Encoded Mixed Batch interoperability peer. MIT. */
#include "../batch_static/Model.hpp"
#include <integration/encoded/BatchDispatcher.hpp>
#include <array>
#include <cstdio>
#include <cstring>

namespace native = static_batch_fixture;
namespace server = telemetry::integration::encoded;
namespace wire = telemetry::protocol::encoded::batch;
using Input = std::span<const std::byte>;
static constexpr wire::Capability capability{17, native::descriptor.fingerprint(), 32, 8192, 8192};

static int digit(char value) noexcept
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

int main(int argc, char** argv)
{
	if (argc == 3) {
		auto* output = std::fopen(argv[2], "wb");
		if (!output)
			return 2;
		std::size_t needed = 0, written = 0;
		if (std::strcmp(argv[1], "--descriptor") == 0) {
			needed = native::descriptorBytes.size();
			written = std::fwrite(native::descriptorBytes.data(), 1, needed, output);
		} else if (std::strcmp(argv[1], "--capability") == 0) {
			std::array<std::byte, wire::capabilityBytes> bytes{};
			const auto result = wire::writeCapability(bytes, capability);
			if (result) {
				needed = result.written;
				written = std::fwrite(bytes.data(), 1, needed, output);
			}
		}
		return std::fclose(output) == 0 && needed && written == needed ? 0 : 2;
	}
	if (argc != 1)
		return 2;
	native::reset();
	std::array<char, 16449> line{};
	std::array<std::byte, 8192> input{}, output{}, scratch{};
	telemetry::Workspace workspace{scratch};
	while (std::fgets(line.data(), static_cast<int>(line.size()), stdin)) {
		auto size = std::strlen(line.data());
		while (size && (line[size - 1] == '\r' || line[size - 1] == '\n'))
			line[--size] = '\0';
		if (std::strcmp(line.data(), "state") == 0) {
			std::printf("{\"number\":%u,\"reads\":%u,\"writes\":%u,\"commands\":%u,"
			            "\"services\":%u,\"decoy\":[%u,%u,%u,%u],\"trace\":[",
			            native::device.current.number, native::device.reads, native::device.writes,
			            native::device.commands, native::device.services, native::decoy.reads,
			            native::decoy.writes, native::decoy.commands, native::decoy.services);
			for (unsigned i = 0; i < native::traceCount; ++i) {
				const auto& event = native::trace[i];
				std::printf("%s[%u,%u,%u,%u]", i ? "," : "", event.operation, event.group,
				            event.index, event.number);
			}
			std::puts("]}");
		} else if (std::strcmp(line.data(), "reset") == 0) {
			native::reset();
			std::puts("{}");
		} else {
			if (!size || size % 2 || size / 2 > input.size())
				return 3;
			for (std::size_t i = 0; i < size / 2; ++i) {
				const auto high = digit(line[2 * i]), low = digit(line[2 * i + 1]);
				if (high < 0 || low < 0)
					return 3;
				input[i] = std::byte((high << 4) | low);
			}
			const auto reply =
			    server::dispatchEncoded(native::model.view(), workspace, 17,
			                            Input{input}.first(size / 2), output, capability);
			if (!reply)
				return 4;
			for (std::size_t i = 0; i < reply.written; ++i)
				std::printf("%02x", std::to_integer<unsigned>(output[i]));
			std::putchar('\n');
		}
		std::fflush(stdout);
	}
	return std::ferror(stdin) ? 5 : 0;
}
