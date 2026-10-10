/*
 * @file ConvenienceNegative.cpp
 * @brief Intended rejection cases for scoped decode ownership and mutable destinations.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>
#include <utility>

struct Reply {
	std::uint32_t value;
};

using Decoded = telemetry::Decoded<Reply>;

void probe(std::span<const std::byte> bytes, [[maybe_unused]] telemetry::Workspace& workspace)
{
#if CASE == 1
	const Reply output{};
	(void)telemetry::decode(bytes, output);
#elif CASE == 2
	volatile Reply output{};
	(void)telemetry::decode(bytes, output);
#elif CASE == 3
	auto result = telemetry::decode<Reply>(bytes, workspace);
	(void)std::move(result).value();
#elif CASE == 4
	const auto result = telemetry::decode<Reply>(bytes, workspace);
	(void)std::move(result).value();
#elif CASE == 5
	(void)telemetry::decode<Reply>(bytes, workspace).value();
#elif CASE == 6
	(void)[&]()->const Decoded
	{
		return telemetry::decode<Reply>(bytes, workspace);
	}
	().value();
#elif CASE == 7
	auto original = telemetry::decode<Reply>(bytes, workspace);
	Decoded copied(original);
	(void)copied;
#elif CASE == 8
	auto original = telemetry::decode<Reply>(bytes, workspace);
	Decoded moved(std::move(original));
	(void)moved;
#elif CASE == 9
	auto first = telemetry::decode<Reply>(bytes, workspace);
	auto second = telemetry::decode<Reply>(bytes, workspace);
	first = second;
#elif CASE == 10
	auto first = telemetry::decode<Reply>(bytes, workspace);
	auto second = telemetry::decode<Reply>(bytes, workspace);
	first = std::move(second);
#endif
}
