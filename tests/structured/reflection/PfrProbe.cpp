/*
 * @file PfrProbe.cpp
 * @brief Check the C++20 aggregate and enum backends before building a facade.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <boost/pfr/config.hpp>
#include <boost/pfr/core.hpp>
#include <boost/pfr/core_name.hpp>
#include <boost/pfr/tuple_size.hpp>
#include <magic_enum.hpp>

#include <cstddef>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

#if !BOOST_PFR_CORE_NAME_ENABLED
#error "Structured telemetry requires Boost.PFR C++20 field names"
#endif

#if !BOOST_PFR_USE_CPP17 || BOOST_PFR_USE_CPP26_REFLECTION
#error "The production C++20 probe requires PFR's structured-binding engine"
#endif

namespace {

using namespace telemetry_structured_probe;

static_assert(boost::pfr::tuple_size_v<MeterConfig> == 2);
static_assert(boost::pfr::get_name<0, MeterConfig>() == "voltage");
static_assert(boost::pfr::get_name<1, MeterConfig>() == "rpm");
static_assert(boost::pfr::get_name<0, RepeatedTypes>() == "phaseA");
static_assert(boost::pfr::get_name<1, RepeatedTypes>() == "phaseB");
static_assert(boost::pfr::get_name<0, Nested>() == "config");
static_assert(boost::pfr::get_name<1, Nested>() == "samples");
static_assert(boost::pfr::tuple_size_v<Nested> == 2);

static_assert(std::is_same_v<decltype(boost::pfr::get<0>(std::declval<MeterConfig&>())), float&>);
static_assert(std::is_same_v<decltype(boost::pfr::get<0>(std::declval<const MeterConfig&>())), const float&>);
static_assert(std::is_same_v<decltype(boost::pfr::get<1>(std::declval<Nested&>())),
                             std::array<std::uint16_t, 3>&>);

static_assert(asciiIdentifier(boost::pfr::get_name<0, MeterConfig>()));
static_assert(!asciiIdentifier(boost::pfr::get_name<0, NonAsciiName>()));
static_assert(asciiIdentifier("_field9"));
static_assert(!asciiIdentifier("9field"));
static_assert(std::string_view{"\xCE\x94Limit"}.size() == 7);

static_assert(magic_enum::enum_count<Mode>() == 3);
static_assert(magic_enum::enum_name(Mode::Auto) == "Auto");
static_assert(magic_enum::enum_name(SignedMode::Below) == "Below");
static_assert(magic_enum::enum_name(SignedMode::Above) == "Above");

// The ordinary scan is bounded; naming a chosen sparse value is separate.
static_assert(magic_enum::enum_count<SparseMode>() == 1);
static_assert(magic_enum::enum_name(SparseMode::Far).empty());
static_assert(magic_enum::enum_name<SparseMode::Far>() == "Far");

// Equal-valued aliases cannot produce two distinct numeric dictionary keys.
static_assert(magic_enum::enum_count<Aliases>() == 1);
static_assert(!magic_enum::enum_name(Aliases::Ready).empty());

} // namespace

extern "C" int structured_probe_other() noexcept;

int main()
{
    constexpr auto aliasName = magic_enum::enum_name(Aliases::Ready);
    std::printf("PFR=%.*s,%.*s; sparse=%.*s; alias=%.*s\n",
                static_cast<int>(boost::pfr::get_name<0, MeterConfig>().size()),
                boost::pfr::get_name<0, MeterConfig>().data(),
                static_cast<int>(boost::pfr::get_name<1, MeterConfig>().size()),
                boost::pfr::get_name<1, MeterConfig>().data(),
                static_cast<int>(magic_enum::enum_name<SparseMode::Far>().size()),
                magic_enum::enum_name<SparseMode::Far>().data(),
                static_cast<int>(aliasName.size()), aliasName.data());
    return structured_probe_other() == 2 ? 0 : 1;
}
