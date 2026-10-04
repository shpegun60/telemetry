/*
 * @file ModelNegative.cpp
 * @brief Compile-time rejection of invalid table positions and lifetimes.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/model/Model.hpp>

#include <cstdint>

namespace ts = telemetry;

struct Request {
	std::uint8_t value;
};

struct Response {
	std::uint8_t value;
};

Response readValue(const Request& request) noexcept
{
	return {request.value};
}

inline constexpr ts::ServiceTable local{ts::service<&readValue>("Read")};
inline constexpr ts::ServiceCatalogTable global{ts::group("group", local)};
enum class BigPosition : std::uint64_t {
	Invalid = 0x100000000ULL
};
enum class LocalPosition : std::uint8_t {
	First = 0
};

#if CASE == 1
auto bad = local.call<1>(Request{});
#elif CASE == 2
auto bad = local.call<BigPosition::Invalid>(Request{});
#elif CASE == 3
auto bad = global.call<telemetry::makeId<1, 0>()>(Request{});
#elif CASE == 4
auto bad = global.call<telemetry::makeId<0, 1>()>(Request{});
#elif CASE == 5
auto bad = global.call<LocalPosition::First>(Request{});
#elif CASE == 6
ts::ServiceTable bad{local};
#elif CASE == 7
auto bad = ts::group("temporary", ts::ServiceTable{ts::service<&readValue>("Read")});
#elif CASE == 8
auto bad = global.index().find<std::uint16_t>(65537);
#elif CASE == 9
auto bad =
    ts::Model{ts::emptyFields, ts::emptyCommands, global}.view().serviceTypeIds<std::uint16_t>(
        65537);
#elif CASE == 10
auto bad = ts::ServiceTable{ts::service<&readValue>("Read")}.data();
#elif CASE == 11
auto bad = ts::ServiceCatalogTable{ts::group("group", local)}.index();
#elif CASE == 12
auto bad = ts::Model{ts::emptyFields, ts::emptyCommands,
                     ts::ServiceCatalogTable{ts::group("group", local)}};
#elif CASE == 13
auto bad = ts::Model{ts::EmptyEndpointCatalog{}, ts::emptyCommands, global};
#else
#error Select a negative case
#endif
