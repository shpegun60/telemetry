/*
 * @file Export.cpp
 * @brief Save an actual native model descriptor for the SDK build example.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "../structured_client/Device.hpp"
#include <fstream>

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	std::ofstream output(argv[1], std::ios::binary);
	const auto& bytes = client_example::descriptorBytes;
	output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	return output ? 0 : 1;
}
