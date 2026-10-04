/*
 * @file PfrOther.cpp
 * @brief Verify identical reflected declarations in a separate translation unit.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <boost/pfr/core_name.hpp>
#include <boost/pfr/tuple_size.hpp>
#include <magic_enum.hpp>

using namespace telemetry_structured_probe;

static_assert(boost::pfr::tuple_size_v<MeterConfig> == 2);
static_assert(boost::pfr::get_name<0, MeterConfig>() == "voltage");
static_assert(boost::pfr::get_name<1, MeterConfig>() == "rpm");
static_assert(magic_enum::enum_name<Mode::Manual>() == "Manual");

extern "C" int structured_probe_other() noexcept
{
	return static_cast<int>(boost::pfr::tuple_size_v<MeterConfig>);
}
