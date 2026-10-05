/*
 * Private business implementation and table storage for module A.
 *
 * Constant initialization avoids cross-TU startup ordering; calls are serialized
 * by the fixture, so the private mutable value requires no shared scratch or heap.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
#include "ModuleA.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace modular::moduleA {
namespace {
constinit std::uint32_t current = 10;
constexpr std::array versionBytes{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};

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
constinit const resource::BytesFile version{versionBytes};

} // namespace modular::moduleA
