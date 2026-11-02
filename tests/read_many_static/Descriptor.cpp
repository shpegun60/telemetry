/* Export the actual Model Descriptor for the unchanged SDK generator. MIT. */
#include "Model.hpp"
#include <cstdio>

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	auto* file = std::fopen(argv[1], "wb");
	if (!file)
		return 3;
	const auto bytes = static_many_fixture::descriptorBytes;
	const auto written = std::fwrite(bytes.data(), 1, bytes.size(), file);
	const auto closed = std::fclose(file);
	return written == bytes.size() && closed == 0 ? 0 : 4;
}
