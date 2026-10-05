/*
 * Private business implementation and table storage for module B.
 *
 * Constant initialization avoids cross-TU startup ordering; calls are serialized
 * by the fixture, so the private mutable value requires no shared scratch or heap.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "ModuleB.hpp"
#include <cstdint>

namespace modular::moduleB {
namespace {
constinit std::uint32_t current = 20;

} // namespace

Reading read() noexcept
{
	return {current};
}

telemetry::WriteResult write(const Reading& value) noexcept
{
	current = value.value;
	return telemetry::WriteResult::Applied;
}

telemetry::CommandResult advance(const Delta& request) noexcept
{
	current += request.amount;
	return telemetry::CommandResult::Executed;
}

Reading inspect(const Query& request) noexcept
{
	return {current * request.factor};
}

constinit const Fields fields{telemetry::field<&read, &write>("Value")};
constinit const Commands commands{telemetry::command<&advance>("Advance")};
constinit const Services services{telemetry::service<&inspect>("Inspect")};

} // namespace modular::moduleB
