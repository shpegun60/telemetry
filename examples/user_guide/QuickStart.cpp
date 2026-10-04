/*
 * Small native Fields, Commands and Services application.
 *
 * Start with ordinary owner methods and bind names directly to their signatures.
 * The static owner and tables satisfy borrowing lifetimes; semantic validation
 * stays in Device rather than the structural Model.
 *
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

#include <telemetry/Telemetry.hpp>
#include <cassert>
#include <cmath>
#include <limits>

namespace ts = telemetry;

struct Config {
	float voltage;
	bool enabled;
};

struct Snapshot {
	float voltage;
	bool enabled;
};

// Public methods:
// - read(): Copy current configuration.
// - write(): Apply validated configuration.
// - reset(): Restore example defaults.
// - sample(): Copy measurement snapshot.
struct Device {
	Config state{230.0f, true};

	Config read() const noexcept
	{
		return state;
	}

	ts::WriteResult write(const Config& next) noexcept
	{
		if (!std::isfinite(next.voltage) || next.voltage < 0.0f || next.voltage > 300.0f)
			return ts::WriteResult::InvalidValue;
		state = next;
		return ts::WriteResult::Applied;
	}

	ts::CommandResult reset() noexcept
	{
		state = {230.0f, true};
		return ts::CommandResult::Executed;
	}

	Snapshot sample() const noexcept
	{
		return {state.voltage, state.enabled};
	}
};

inline Device device;
inline constexpr ts::FieldTable localFields{
    ts::field<&Device::read, &Device::write>("Config", device)};
inline constexpr ts::CommandTable localCommands{ts::command<&Device::reset>("Reset", device)};
inline constexpr ts::ServiceTable localServices{ts::service<&Device::sample>("Sample", device)};
inline constexpr ts::FieldCatalogTable fields{ts::group("device", localFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("device", localCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("device", localServices)};
inline constexpr ts::Model model{fields, commands, services};

int main()
{
	assert(localFields.write<0>(Config{240.0f, true}) == ts::WriteResult::Applied);
	const auto value = fields.read<ts::makeId<0, 0>()>();
	assert(value && value->voltage == 240.0f);
	assert(localFields.write<0>(Config{std::numeric_limits<float>::quiet_NaN(), false}) ==
	       ts::WriteResult::InvalidValue);
	const auto unchanged = localFields.read<0>();
	assert(unchanged && unchanged->voltage == 240.0f && unchanged->enabled);
	const auto response = services.call<ts::makeId<0, 0>()>();
	assert(response.hasValue() && response.value().voltage == 240.0f);
	assert(localCommands.call<0>() == ts::CommandResult::Executed);
}
