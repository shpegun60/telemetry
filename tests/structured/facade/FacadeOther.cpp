/*
 * @file FacadeOther.cpp
 * @brief Repeat the normalized facade in a separate translation unit.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "../reflection/ProbeTypes.hpp"

#include <telemetry/reflection/Reflection.hpp>

using telemetry_structured_probe::MeterConfig;
namespace refl = telemetry::reflection;

static_assert(refl::memberCount<MeterConfig> == 2);
static_assert(refl::memberName<0, MeterConfig>() == "voltage");
static_assert(refl::memberName<1, MeterConfig>() == "rpm");

extern "C" int structured_facade_other() noexcept
{
	return static_cast<int>(refl::memberCount<MeterConfig>);
}
