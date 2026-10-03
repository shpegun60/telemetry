/*
 * @file RegistryArm.cpp
 * @brief Force emission of the immutable registry for ARM ELF section checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <telemetry/type/Registry.hpp>

using Registry = telemetry::TypeRegistry<
    registry_probe::SampleBlock, registry_probe::SameShape>;

extern "C" telemetry::TypeRegistryView structured_registry_view() noexcept
{
    return Registry::view();
}
