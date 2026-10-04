/*
 * @file ArmCodegen.cpp
 * @brief Direct/local/global native codegen and actual runtime entry layouts.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/model/Model.hpp>
#include "MixedFixture.hpp"
namespace ts = telemetry;

namespace probe {
struct Request {
	std::uint32_t value;
};

// Counted native owner supplies direct, local-table and global-catalog ARM roots.
// API: get(), set(), call().
struct Device {
	std::uint32_t value = 9;

	std::uint32_t get() const noexcept
	{
		return value;
	}

	telemetry::WriteResult set(std::uint32_t next) noexcept
	{
		value = next;
		return telemetry::WriteResult::Applied;
	}

	telemetry::CommandResult call(const Request& request) noexcept
	{
		value = request.value;
		return telemetry::CommandResult::Executed;
	}
};

Device device;
inline constexpr ts::FieldTable fields{ts::field<&Device::get, &Device::set>("value", device)};
inline constexpr ts::CommandTable commands{ts::command<&Device::call>("set", device)};
inline constexpr ts::FieldCatalogTable fieldCatalogs{ts::group("test", fields)};
inline constexpr ts::CommandCatalogTable commandCatalogs{ts::group("test", commands)};
} // namespace probe

#define PROBE extern "C" __attribute__((noinline))

PROBE std::optional<std::uint32_t> read_direct() noexcept
{
	return probe::device.get();
}

PROBE std::optional<std::uint32_t> read_local() noexcept
{
	return probe::fields.read<0>();
}

PROBE std::optional<std::uint32_t> read_global() noexcept
{
	return probe::fieldCatalogs.read<0>();
}

PROBE telemetry::WriteResult write_direct(std::uint32_t value) noexcept
{
	return probe::device.set(value);
}

PROBE telemetry::WriteResult write_local(std::uint32_t value) noexcept
{
	return probe::fields.write<0>(value);
}

PROBE telemetry::WriteResult write_global(std::uint32_t value) noexcept
{
	return probe::fieldCatalogs.write<0>(value);
}

PROBE telemetry::CommandResult command_direct(const probe::Request& value) noexcept
{
	return probe::device.call(value);
}

PROBE telemetry::CommandResult command_local(const probe::Request& value) noexcept
{
	return probe::commands.call<0>(value);
}

PROBE telemetry::CommandResult command_global(const probe::Request& value) noexcept
{
	return probe::commandCatalogs.call<0>(value);
}

// Return-type and data-shape differences must not reintroduce runtime type
// switches when the position and binding are known to the compiler.
#define MIXED_READ(label, index, method, Value)                                                    \
	PROBE std::optional<Value> label##_direct() noexcept                                           \
	{                                                                                              \
		return fixture::device.method();                                                           \
	}                                                                                              \
	PROBE std::optional<Value> label##_local() noexcept                                            \
	{                                                                                              \
		return fixture::mixedFields.read<index>();                                                 \
	}                                                                                              \
	PROBE std::optional<Value> label##_global() noexcept                                           \
	{                                                                                              \
		return fixture::fields.read<index>();                                                      \
	}
MIXED_READ(boolean, 0, enabled, bool)
MIXED_READ(u16, 1, rpm, std::uint16_t)
MIXED_READ(f32, 2, temperature, float)
MIXED_READ(f64, 3, precise, double)
MIXED_READ(enumeration, 4, mode, fixture::Mode)
using Samples = std::array<std::uint16_t, 3>;
MIXED_READ(array, 5, samples, Samples)
MIXED_READ(state, 6, state, fixture::State)
MIXED_READ(config, 7, config, fixture::MotorConfig)

#define ENTRY_LAYOUT(T, Context) sizeof(ts::T), alignof(ts::T), offsetof(ts::T, Context)
extern "C" __attribute__((used, section(".rodata.endpoint_layout")))
const std::uint32_t endpoint_layout[] = {
    ENTRY_LAYOUT(FieldEntry, readContext), offsetof(ts::FieldEntry, read),
    offsetof(ts::FieldEntry, write),       ENTRY_LAYOUT(CommandEntry, context),
    offsetof(ts::CommandEntry, invoke),    ENTRY_LAYOUT(ServiceEntry, context),
    offsetof(ts::ServiceEntry, invoke)};
