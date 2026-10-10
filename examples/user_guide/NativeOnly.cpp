/*
 * @file NativeOnly.cpp
 * @brief Local Field, Command and Service calls using ordinary C++ values.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 *
 * Build this translation unit with the documented include paths. It creates
 * no Model, Workspace or files and links no compiled telemetry source.
 */

#include <telemetry/Telemetry.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace ts = telemetry;

namespace {
unsigned checks = 0;
#define CHECK(expression)                                                                          \
	do {                                                                                           \
		++checks;                                                                                  \
		assert((expression));                                                                      \
	} while (false)

struct Query {
	std::uint32_t channel;
};

struct Reply {
	std::uint32_t period;
	std::uint32_t channel;
};

// Identical member layout does not make this the same native request type.
struct OtherQuery {
	std::uint32_t channel;
};

// The application owns state and decides which values it accepts.
// Public methods: period(): Read period; setPeriod(): Validate update;
// reset(): Restore state; inspect(): Report channel.
class Device {
public:
	std::uint32_t period() const noexcept
	{
		return period_;
	}

	ts::WriteResult setPeriod(std::uint32_t value) noexcept
	{
		if (value == 0 || value > 1000)
			return ts::WriteResult::InvalidValue;
		period_ = value;
		return ts::WriteResult::Applied;
	}

	ts::CommandResult reset() noexcept
	{
		period_ = 10;
		return ts::CommandResult::Executed;
	}

	ts::ServiceResult<Reply> inspect(const Query& query) const noexcept
	{
		if (query.channel > 3)
			return ts::ServiceResult<Reply>::failure(ts::ServiceStatus::InvalidArgument);
		return ts::ServiceResult<Reply>::success({period_, query.channel});
	}

private:
	std::uint32_t period_ = 10;
};

// Keep the owner alive for every call through these borrowed bindings.
Device device;
inline constexpr ts::FieldTable fields{
    ts::field<&Device::period, &Device::setPeriod>("Period", device)};
inline constexpr ts::CommandTable commands{ts::command<&Device::reset>("Reset", device)};
inline constexpr ts::ServiceTable services{ts::service<&Device::inspect>("Inspect", device)};
} // namespace

int main()
{
	// Compile-time positions retain the exact definition and native result.
	const auto initial = fields.read<0>();
	CHECK(initial && *initial == 10);
	CHECK(fields.write<0>(std::uint32_t{25}) == ts::WriteResult::Applied);
	const auto response = services.call<0>(Query{2});
	CHECK(response.hasValue());
	CHECK(response.status() == ts::ServiceStatus::Ok);
	CHECK(response.value().period == 25 && response.value().channel == 2);
	CHECK(fields.write<0>(std::uint32_t{0}) == ts::WriteResult::InvalidValue);
	CHECK(device.period() == 25);
	const auto refused = services.call<0>(Query{4});
	CHECK(!refused.hasValue() && refused.status() == ts::ServiceStatus::InvalidArgument);
	CHECK(commands.call<0>() == ts::CommandResult::Executed);
	CHECK(device.period() == 10);

	// Runtime Field access supports checked numeric conversions.
	std::uint32_t position = 0;
	CHECK(fields.writeAs(position, std::uint16_t{50}) == ts::WriteResult::Applied);
	const auto converted = fields.readAs<std::uint16_t>(position);
	CHECK(converted && *converted == 50);
	const auto detailed = fields.readAsResult<std::uint32_t>(position);
	CHECK(detailed.status() == ts::FieldReadStatus::Ok);
	CHECK(detailed.value() == 50);
	CHECK(fields.writeAs(position, -1) == ts::WriteResult::InvalidValue);
	CHECK(device.period() == 50);
	CHECK(fields.writeAs(position, std::uint32_t{1001}) == ts::WriteResult::InvalidValue);
	CHECK(device.period() == 50);
	CHECK(!fields.readAs<std::uint32_t>(1u));
	CHECK(fields.readAsResult<std::uint32_t>(1u).status() == ts::FieldReadStatus::NotFound);
	CHECK(fields.writeAs(1u, std::uint32_t{60}) == ts::WriteResult::NotFound);

	// Runtime Command/Service calls require the exact request signature.
	const auto flat = services.callAs<Reply>(position, Query{1});
	CHECK(flat && flat.status() == ts::ServiceCallStatus::Ok);
	CHECK(flat.value().period == 50 && flat.value().channel == 1);
	CHECK(flat.valueOrNull() == &flat.value());
	const auto flatRefused = services.callAs<Reply>(position, Query{4});
	CHECK(!flatRefused && flatRefused.status() == ts::ServiceCallStatus::InvalidArgument);
	CHECK(flatRefused.valueOrNull() == nullptr);
	CHECK(services.callAs<Reply>(position, OtherQuery{1}).status() ==
	      ts::ServiceCallStatus::SignatureMismatch);
	CHECK(services.callAs<Reply>(1u, Query{1}).status() == ts::ServiceCallStatus::NotFound);
	CHECK(commands.call(position, Query{1}) == ts::CommandCallStatus::SignatureMismatch);
	CHECK(commands.call(1u) == ts::CommandCallStatus::NotFound);
	CHECK(commands.call(position) == ts::CommandCallStatus::Executed);
	CHECK(device.period() == 10);
	CHECK(flat.value().period == 50); // Owning result keeps its earlier snapshot.

	std::printf("Native only guide: %u checks passed\n", checks);
}
