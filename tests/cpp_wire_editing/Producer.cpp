/* Export an actual native Descriptor; no wire schema is reconstructed here.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "Fixture.hpp"
#include <fstream>

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	std::ofstream output(argv[1], std::ios::binary);
	output.write(reinterpret_cast<const char*>(wire_fixture::descriptorBytes.data()),
	             static_cast<std::streamsize>(wire_fixture::descriptorBytes.size()));
	return output ? 0 : 3;
}
