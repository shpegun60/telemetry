/* Selected standalone metadata root, without DTOs or SDK includes. MIT. */
#include <edge_api/Metadata.hpp>

namespace md = edge_api::metadata;
volatile std::uint32_t metadataChecksum;

extern "C" void metadata_probe()
{
	std::uint32_t value = 0;
	const auto text = [&value](std::string_view name) {
		for (unsigned char byte : name)
			value = value * 33 + byte;
	};
	for (auto byte : md::descriptorHeader)
		value = value * 33 + byte;
	for (auto row : md::typeRecords)
		value += row.id + row.kind + row.scalarCode + row.wireBytes + row.relatedType +
		         row.elementCount + row.memberFirst + row.memberCount + row.enumFirst +
		         row.enumCount;
	for (auto row : md::memberRecords) {
		value += row.ownerType + row.typeId + row.wireOffset;
		text(row.name);
		text(row.cppName);
	}
	for (auto row : md::enumRecords) {
		value += row.ownerType + static_cast<std::uint32_t>(row.bits) +
		         static_cast<std::uint32_t>(row.bits >> 32);
		text(row.name);
	}
	for (auto row : md::catalogs) {
		value += row.category + row.group + row.firstInCategory + row.count;
		text(row.name);
		text(row.symbol);
	}
	for (auto row : md::endpoints) {
		value += row.category + row.id + row.catalogIndex + row.valueType + row.requestType +
		         row.responseType + row.capabilities;
		text(row.catalogName);
		text(row.name);
		text(row.groupSymbol);
		text(row.symbol);
	}
	metadataChecksum = value;
}

#ifndef TELEMETRY_METADATA_ARM_PROBE
int main()
{
	metadata_probe();
	return 0;
}
#endif
