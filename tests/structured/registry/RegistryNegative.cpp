/*
 * @file RegistryNegative.cpp
 * @brief Focused compile-time rejection checks for TypeRegistry lookup.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <telemetry_structured/type/Registry.hpp>

using Registry = telemetry::structured::TypeRegistry<registry_probe::Reading>;

#if CASE == 1
constexpr auto bad = Registry::typeId<registry_probe::SameShape>();
#elif CASE == 2
constexpr auto bad = Registry::descriptor<13>();
#else
#error Select a negative case
#endif
