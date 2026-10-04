/*
 * Validate and update application state for the integration example.
 *
 * Apply semantic checks before replacing configuration blocks. Counters record
 * attempts separately from accepted state changes, allowing the executable
 * example to show whether packet preflight reached business code.
 *
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

#include "Device.hpp"
#include <algorithm>

namespace app {
namespace {
bool validPeriod(std::uint32_t value) noexcept
{
	return value >= 1 && value <= 1000;
}

bool validMode(Mode value) noexcept
{
	return value == Mode::Off || value == Mode::Measuring;
}

bool validGains(const Gains& value) noexcept
{
	return std::all_of(value.begin(), value.end(), [](auto gain) {
		return gain >= 1 && gain <= 1000;
	});
}

bool validDisplay(const DisplayConfig& value) noexcept
{
	return value.brightness <= 100;
}
} // namespace

std::uint32_t Device::readPeriod() const noexcept
{
	++counters_.reads;
	return sampling_.periodMs;
}

Mode Device::readMode() const noexcept
{
	++counters_.reads;
	return sampling_.mode;
}

Gains Device::readGains() const noexcept
{
	++counters_.reads;
	return sampling_.gains;
}

DisplayConfig Device::readDisplay() const noexcept
{
	++counters_.reads;
	return display_;
}

WriteStatus Device::writePeriod(std::uint32_t value) noexcept
{
	++counters_.writes;
	if (!validPeriod(value))
		return WriteStatus::InvalidValue;
	sampling_.periodMs = value;
	++changes_;
	return WriteStatus::Applied;
}

WriteStatus Device::writeMode(Mode value) noexcept
{
	++counters_.writes;
	if (!validMode(value))
		return WriteStatus::InvalidValue;
	sampling_.mode = value;
	++changes_;
	return WriteStatus::Applied;
}

WriteStatus Device::writeGains(const Gains& value) noexcept
{
	++counters_.writes;
	if (!validGains(value))
		return WriteStatus::InvalidValue;
	sampling_.gains = value;
	++changes_;
	return WriteStatus::Applied;
}

WriteStatus Device::writeDisplay(const DisplayConfig& value) noexcept
{
	++counters_.writes;
	if (!validDisplay(value))
		return WriteStatus::InvalidValue;
	display_ = value;
	++changes_;
	return WriteStatus::Applied;
}

CommandStatus Device::reset() noexcept
{
	++counters_.commands;
	sampling_ = {10, Mode::Measuring, {100, 100, 100}};
	display_ = {50, true};
	++changes_;
	return CommandStatus::Executed;
}

CommandStatus Device::configure(const Configure& request) noexcept
{
	++counters_.commands;
	if (!validPeriod(request.sampling.periodMs) || !validMode(request.sampling.mode) ||
	    !validGains(request.sampling.gains) || !validDisplay(request.display))
		return CommandStatus::InvalidValue;
	// Validate both blocks before replacing either one.
	sampling_ = request.sampling;
	display_ = request.display;
	++changes_;
	return CommandStatus::Executed;
}

Snapshot Device::sample() const noexcept
{
	++counters_.services;
	return {changes_, sampling_.gains[0], sampling_.mode, display_.enabled};
}

std::optional<Snapshot> Device::query(const Query& request) const noexcept
{
	++counters_.services;
	if (request.channel >= sampling_.gains.size())
		return std::nullopt;
	return Snapshot{changes_, sampling_.gains[request.channel], sampling_.mode, display_.enabled};
}
} // namespace app
