/* Export actual native Descriptor for the unchanged SDK generator. MIT. */
#include "Model.hpp"
#include <cstdio>

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	auto* file = std::fopen(argv[1], "wb");
	if (!file)
		return 3;
	const auto written = std::fwrite(static_batch_fixture::descriptorBytes.data(), 1,
	                                 static_batch_fixture::descriptorBytes.size(), file);
	const auto closed = std::fclose(file);
	return written == static_batch_fixture::descriptorBytes.size() && closed == 0 ? 0 : 4;
}
