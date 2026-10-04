/*
 * @file RegistryOther.cpp
 * @brief Confirm deterministic TypeIds from a separate translation unit.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <telemetry/abi/StructuredAbi.hpp>
#include <telemetry/type/Registry.hpp>

#include <cstdint>

using Registry = telemetry::TypeRegistry<registry_probe::SampleBlock, registry_probe::Reading,
                                         registry_probe::SameShape, const registry_probe::Reading&,
                                         registry_probe::Mode>;

static_assert(Registry::typeId<registry_probe::SampleBlock>() == 15);
static_assert(Registry::descriptor<15>().member(0)->name == "reading");

extern "C" std::uint32_t registry_other_id() noexcept
{
	telemetry::requireStructuredAbi();
	return Registry::typeId<registry_probe::SampleBlock>();
}
