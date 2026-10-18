/* Hex complete-frame subprocess bridge to the persistent actual Model. MIT. */
#include "EncodedFixture.hpp"
#include <resource/telemetry/v3/Descriptor.hpp>
#include <cstdio>
#include <cstring>

using namespace encoded_fixture;

static int digit(char value) noexcept
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--descriptor") == 0) {
		static constexpr server::Descriptor descriptor{model};
		static constexpr auto bytes = server::packDescriptor<descriptor>();
		auto* output = std::fopen(argv[2], "wb");
		if (output == nullptr)
			return 5;
		const bool complete = std::fwrite(bytes.data(), 1, bytes.size(), output) == bytes.size();
		const int closed = std::fclose(output);
		return complete && closed == 0 ? 0 : 5;
	}
	if (argc != 1)
		return 2;
	std::array<char, 2049> line{};
	std::array<std::byte, 1024> frame{}, reply{}, scratch{};
	ts::Workspace workspace{scratch};
	while (std::fgets(line.data(), static_cast<int>(line.size()), stdin) != nullptr) {
		auto length = std::strlen(line.data());
		while (length != 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
			--length;
		if (length == 0 || length % 2 != 0 || length / 2 > frame.size())
			return 2;
		for (std::size_t i = 0; i < length / 2; ++i) {
			const int high = digit(line[2 * i]), low = digit(line[2 * i + 1]);
			if (high < 0 || low < 0)
				return 2;
			frame[i] = std::byte((high << 4) | low);
		}
		const auto result = server::dispatchEncoded(modelView, workspace, 17,
		                                            Input{frame}.first(length / 2), reply);
		if (!result) {
			std::fprintf(stderr, "encoded bridge frame rejected: status=%u wire=%u\n",
			             static_cast<unsigned>(result.status), static_cast<unsigned>(result.wire));
			return 3;
		}
		for (std::size_t i = 0; i < result.written; ++i)
			std::printf("%02x", std::to_integer<unsigned>(reply[i]));
		std::putchar('\n');
		std::fflush(stdout);
	}
	return std::ferror(stdin) ? 4 : 0;
}
