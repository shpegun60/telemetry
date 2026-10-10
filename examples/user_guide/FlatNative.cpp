/*
 * @file FlatNative.cpp
 * @brief One-level runtime Command and owning/borrowed Service results.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/Telemetry.hpp>
#include <cassert>
#include <cstdio>

namespace ts = telemetry;

namespace {
unsigned checks = 0;
#define CHECK(expression)                                                                          \
	do {                                                                                           \
		++checks;                                                                                  \
		assert((expression));                                                                      \
	} while (false)

struct SetPeriod {
	std::uint32_t period;
};

struct Config {
	std::uint32_t period;
};

struct Reply {
	std::uint32_t period;
};

// The application owns its configuration and all borrowed response lifetimes.
// Public methods: configure(): Apply period; inspect(): Report current period;
// config(): Borrow configuration; notify(): Count notifications.
class Device {
public:
	ts::CommandResult configure(const SetPeriod& request) noexcept
	{
		if (request.period == 0)
			return ts::CommandResult::InvalidValue;
		config_.period = request.period;
		return ts::CommandResult::Executed;
	}

	ts::ServiceResult<Reply> inspect(const SetPeriod& request) const noexcept
	{
		if (request.period == 0)
			return ts::ServiceResult<Reply>::failure(ts::ServiceStatus::Busy);
		return ts::ServiceResult<Reply>::success({config_.period});
	}

	const Config& config() const noexcept
	{
		return config_;
	}

	void notify() noexcept
	{
		++notifications;
	}

	unsigned notifications = 0;

private:
	Config config_{10};
};

Device device;
inline constexpr ts::CommandTable localCommands{
    ts::command<&Device::configure>("Configure", device)};
inline constexpr ts::ServiceTable localServices{ts::service<&Device::inspect>("Inspect", device),
                                                ts::service<&Device::config>("Config", device),
                                                ts::service<&Device::notify>("Notify", device)};
inline constexpr ts::CommandCatalogTable commands{ts::group("Device", localCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("Device", localServices)};
} // namespace

int main()
{
	const auto status = commands.call(ts::makeId<0, 0>(), SetPeriod{25});
	CHECK(status == ts::CommandCallStatus::Executed);
	CHECK(commands.call(0, SetPeriod{0}) == ts::CommandCallStatus::InvalidValue);
	CHECK(commands.call(0, Config{25}) == ts::CommandCallStatus::SignatureMismatch);
	CHECK(commands.call(0xffff, SetPeriod{25}) == ts::CommandCallStatus::NotFound);

	const auto response = services.callAs<Reply>(ts::makeId<0, 0>(), SetPeriod{1});
	CHECK(response && response.hasValue());
	CHECK(response.status() == ts::ServiceCallStatus::Ok);
	CHECK(response.value().period == 25);
	CHECK(response.valueOrNull() == &response.value());
	const auto refused = services.callAs<Reply>(0, SetPeriod{0});
	CHECK(!refused && !refused.hasValue());
	CHECK(refused.status() == ts::ServiceCallStatus::Busy && refused.valueOrNull() == nullptr);
	CHECK(services.callAs<Reply>(0, Config{25}).status() ==
	      ts::ServiceCallStatus::SignatureMismatch);
	CHECK(services.callAs<Reply>(0xffff, SetPeriod{1}).status() == ts::ServiceCallStatus::NotFound);

	// Borrowed access neither copies the configuration nor extends its lifetime.
	const auto config = services.callBorrowed<Config>(ts::makeId<0, 1>());
	CHECK(config && config.valueOrNull() == &device.config());
	CHECK(localCommands.call(0, SetPeriod{50}) == ts::CommandCallStatus::Executed);
	CHECK(config.value().period == 50 && response.value().period == 25);
	CHECK(services.callAs<Config>(1).status() == ts::ServiceCallStatus::SignatureMismatch);
	CHECK(services.callBorrowed<Reply>(0, SetPeriod{1}).status() ==
	      ts::ServiceCallStatus::SignatureMismatch);

	const auto notified = services.callAs<void>(ts::makeId<0, 2>());
	CHECK(notified && notified.status() == ts::ServiceCallStatus::Ok);
	notified.value(); // Successful void operation; no payload object.
	CHECK(device.notifications == 1);

	// The old detailed routing API remains available for explicit diagnostics.
	const auto detailed = services.callAs<ts::ServiceResult<Reply>>(0, SetPeriod{0});
	CHECK(detailed.status() == ts::NativeCallStatus::Ok);
	CHECK(detailed.value().status() == ts::ServiceStatus::Busy);
	CHECK((commands.call<ts::makeId<0, 0>()>(SetPeriod{75}) == ts::CommandResult::Executed));
	std::printf("Flat native guide: %u checks passed\n", checks);
}
