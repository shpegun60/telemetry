/*
 * @file RegistryArm.cpp
 * @brief Force emission of the immutable registry for ARM ELF section checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <telemetry_structured/type/Registry.hpp>

using Registry = telemetry::structured::TypeRegistry<
    registry_probe::SampleBlock, registry_probe::SameShape>;

extern "C" telemetry::structured::TypeRegistryView structured_registry_view() noexcept
{
    return Registry::view();
}
