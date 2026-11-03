/* Independent installed-free structural metadata consumer. Authors: Ruslan Kovtun, codexAi. MIT. */
#include <edge_api/Endpoints.hpp>
#include <edge_api/Metadata.hpp>

#include <cstdint>
#include <iostream>
#include <string_view>

namespace md = edge_api::metadata;

void name(std::string_view value)
{
	constexpr char digits[] = "0123456789abcdef";
	for (unsigned char byte : value)
		std::cout << digits[byte >> 4] << digits[byte & 15];
	std::cout << '\t';
}

int main()
{
	static_assert(md::version == 1 && md::wireMajor == 3 && md::wireMinor == 0);
	for (auto row : md::typeRecords)
		std::cout << "T\t" << row.id << '\t' << unsigned(row.kind) << '\t'
		          << unsigned(row.scalarCode) << '\t' << row.wireBytes << '\t' << row.relatedType
		          << '\t' << row.elementCount << '\t' << row.memberFirst << '\t' << row.memberCount
		          << '\t' << row.enumFirst << '\t' << row.enumCount << '\n';
	for (auto row : md::memberRecords) {
		std::cout << "M\t" << row.ownerType << '\t' << row.typeId << '\t' << row.wireOffset << '\t';
		name(row.name);
		name(row.cppName);
		std::cout << '\n';
	}
	for (auto row : md::enumRecords) {
		std::cout << "E\t" << row.ownerType << '\t' << row.bits << '\t';
		name(row.name);
		std::cout << '\n';
	}
	for (auto row : md::catalogs) {
		std::cout << "C\t" << unsigned(row.category) << '\t' << row.group << '\t'
		          << row.firstInCategory << '\t' << row.count << '\t';
		name(row.name);
		name(row.symbol);
		std::cout << '\n';
	}
	for (auto row : md::endpoints) {
		std::cout << "P\t" << unsigned(row.category) << '\t' << row.id << '\t' << row.catalogIndex
		          << '\t' << row.valueType << '\t' << row.requestType << '\t' << row.responseType
		          << '\t' << unsigned(row.capabilities) << '\t';
		name(row.catalogName);
		name(row.name);
		name(row.groupSymbol);
		name(row.symbol);
		std::cout << '\n';
	}
}
