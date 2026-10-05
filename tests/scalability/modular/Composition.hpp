/*
 * One typed composition point for all private module schemas.
 *
 * The typed-header build includes this in every client; the erased-boundary
 * build includes it only in Composition.cpp. One Registry deduplicates shared
 * requests while retaining each module's exact Reading type.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#ifndef TELEMETRY_TESTS_SCALABILITY_MODULAR_COMPOSITION_HPP
#define TELEMETRY_TESTS_SCALABILITY_MODULAR_COMPOSITION_HPP
#pragma once

#include "ModuleA.hpp"
#include "ModuleB.hpp"
#include "ModuleC.hpp"
#include <telemetry/model/Model.hpp>

namespace modular::composition {
inline constinit const telemetry::FieldCatalogTable fields{telemetry::group("A", moduleA::fields),
                                                           telemetry::group("B", moduleB::fields),
                                                           telemetry::group("C", moduleC::fields)};
inline constinit const telemetry::CommandCatalogTable commands{
    telemetry::group("A", moduleA::commands), telemetry::group("B", moduleB::commands),
    telemetry::group("C", moduleC::commands)};
inline constinit const telemetry::ServiceCatalogTable services{
    telemetry::group("A", moduleA::services), telemetry::group("B", moduleB::services),
    telemetry::group("C", moduleC::services)};
inline constexpr telemetry::Model model{fields, commands, services};
using Registry = decltype(model)::Registry;
static_assert(Registry::typeCount == 17);
static_assert(Registry::typeId<moduleA::Reading>() != Registry::typeId<moduleB::Reading>());
static_assert(Registry::typeId<moduleB::Reading>() != Registry::typeId<moduleC::Reading>());
} // namespace modular::composition
#endif
