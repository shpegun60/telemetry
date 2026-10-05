/*
 * @file Ergonomics.cpp
 * @brief Runtime native calls, borrowing, error details and catalog traversal.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/Telemetry.hpp>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace ts = telemetry;

namespace {
unsigned checks = 0;
#define CHECK(expression)                                                                          \
	do {                                                                                           \
		++checks;                                                                                  \
		assert((expression));                                                                      \
	} while (false)

struct Config {
	std::uint32_t period;
};

struct Request {
	std::uint32_t period;
};

struct Reply {
	std::uint32_t period;
};

class Device {
public:
	// Public methods: config(): Borrow settings; configure(): Apply settings;
	// inspect(): Return application result. Device owns all borrowed state.
	const Config& config() const noexcept
	{
		return config_;
	}

	ts::CommandResult configure(const Request& request) noexcept
	{
		if (request.period == 0)
			return ts::CommandResult::InvalidValue;
		config_.period = request.period;
		return ts::CommandResult::Executed;
	}

	ts::ServiceResult<Reply> inspect(const Request& request) const noexcept
	{
		if (request.period == 0)
			return ts::ServiceResult<Reply>::failure(ts::ServiceStatus::Busy);
		return ts::ServiceResult<Reply>::success({config_.period});
	}

private:
	Config config_{10};
};

Device device;
inline constexpr ts::FieldTable localFields{ts::field<&Device::config>("Config", device)};
inline constexpr ts::CommandTable localCommands{
    ts::command<&Device::configure>("Configure", device)};
inline constexpr ts::ServiceTable localServices{ts::service<&Device::inspect>("Inspect", device)};
inline constexpr ts::FieldCatalogTable fields{ts::group("Device", localFields)};
inline constexpr ts::CommandCatalogTable commands{ts::group("Device", localCommands)};
inline constexpr ts::ServiceCatalogTable services{ts::group("Device", localServices)};
inline constexpr ts::Model model{fields, commands, services};
} // namespace

int main()
{
	const ts::PackedId id = ts::makeId<0, 0>();
	const auto config = fields.readBorrowed<Config>(id);
	CHECK(config && config.valueOrNull() == &device.config());
	const auto copy = fields.readAsResult<Config>(id);
	CHECK(copy.status() == ts::FieldReadStatus::Ok && copy.value().period == 10);
	CHECK(fields.readAsResult<double>(id).status() == ts::FieldReadStatus::TypeMismatch);
	CHECK(fields.readAsResult<Config>(0xffff).status() == ts::FieldReadStatus::NotFound);
	const auto command = commands.callAs(id, Request{25});
	CHECK(command.status() == ts::NativeCallStatus::Ok);
	CHECK(command.value() == ts::CommandResult::Executed);
	CHECK(config.value().period == 25 && copy.value().period == 10);
	CHECK(commands.callAs(id, Config{25}).status() == ts::NativeCallStatus::SignatureMismatch);
	const auto reply = services.callAs<ts::ServiceResult<Reply>>(id, Request{1});
	CHECK(reply.hasValue() && reply.value().hasValue());
	CHECK(reply.value().value().period == 25);
	const auto refused = services.callAs<ts::ServiceResult<Reply>>(id, Request{0});
	CHECK(refused.hasValue() && refused.value().status() == ts::ServiceStatus::Busy);
	unsigned entries = 0;
	fields.forEachEntry(
	    [&](ts::PackedId selected, std::string_view group, const ts::FieldEntry& row) {
		    CHECK(selected == id && group == "Device" && std::string_view{row.name} == "Config");
		    ++entries;
	    });
	CHECK(entries == 1);
	unsigned visits = 0;
	CHECK(!fields.forEachWhile([&](std::string_view, const auto&) {
		++visits;
		return false;
	}));
	CHECK(visits == 1);
	static_assert(model.maxCommandRequestWireSize() == ts::wireSize<Request>);
	static_assert(model.maxServiceRequestWireSize() == ts::wireSize<Request>);
	std::printf("Ergonomics guide: %u checks passed\n", checks);
}
