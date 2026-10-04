/*
 * Native C++20 endpoint, catalog, reflection and slot examples.
 *
 * Exercise compile-time calls and runtime encoded access over one set of
 * stable owners. Borrowed values retain their owner lifetime; Workspace leases
 * bound decoded objects and release scratch in reverse acquisition order.
 *
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

#include <telemetry/Telemetry.hpp>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string_view>
#include <type_traits>

namespace guide {
enum class Mode : std::int16_t {
	Off = 0,
	Running = 1,
	Standby = 3000
};
enum class Label : std::uint16_t {
	Normal = 0,
	Remote = 1000
};

struct Calibration {
	float gain;
	float offset;
};

struct Settings {
	Calibration calibration;
	Mode mode;
	std::array<std::uint16_t, 3> limits;
};

struct Query {
	std::uint16_t channel;
};

struct Configure {
	Settings settings;
};

struct Snapshot {
	float voltage;
	Mode mode;
};

struct Block {
	std::array<std::uint32_t, 64> words;
};
} // namespace guide

namespace telemetry::reflection {
template<>
struct EnumReflection<guide::Mode> {
	inline static constexpr auto entries =
	    enumCodes<guide::Mode::Off, guide::Mode::Running, guide::Mode::Standby>();
};

template<>
struct EnumReflection<guide::Label> {
	inline static constexpr auto entries =
	    enumEntries(enumEntry(guide::Label::Normal, u8"Звичайний"),
	                enumEntry(guide::Label::Remote, u8"Віддалений"));
};
} // namespace telemetry::reflection

namespace guide {
using telemetry::CommandResult;
using telemetry::WriteResult;
using telemetry::ServiceStatus;

// Public methods:
// - readVoltage(): Read native voltage.
// - writeVoltage(): Validate voltage update.
// - readSettings(): Copy current settings.
// - borrowSettings(): Borrow current settings.
// - writeSettings(): Replace current settings.
// - readBlock(): Borrow large block.
// - writeBlock(): Replace large block.
// - readLimits(): Copy native limits.
// - reset(): Count reset operation.
// - configure(): Replace current settings.
// - select(): Validate channel request.
// - sample(): Copy current snapshot.
// - query(): Query owning snapshot.
// - live(): Query borrowed settings.
// - ping(): Count statusless call.
// - validate(): Check request channel.
// - echoBlock(): Borrow caller block.
struct Device {
	float voltage = 230.0f;
	Settings settings{{1.0f, 0.0f}, Mode::Running, {10, 20, 30}};
	Block block{};
	unsigned resets = 0;
	bool busy = false;

	float readVoltage() const noexcept
	{
		return voltage;
	}

	WriteResult writeVoltage(float value) noexcept
	{
		if (!std::isfinite(value) || value < 0.0f || value > 300.0f)
			return WriteResult::InvalidValue;
		if (busy)
			return WriteResult::Busy;
		voltage = value;
		return WriteResult::Applied;
	}

	Settings readSettings() const noexcept
	{
		return settings;
	}

	const Settings& borrowSettings() const noexcept
	{
		return settings;
	}

	WriteResult writeSettings(const Settings& value) noexcept
	{
		settings = value;
		return WriteResult::Applied;
	}

	const Block& readBlock() const noexcept
	{
		return block;
	}

	WriteResult writeBlock(const Block& value) noexcept
	{
		block = value;
		return WriteResult::Applied;
	}

	std::array<std::uint16_t, 3> readLimits() const noexcept
	{
		return settings.limits;
	}

	CommandResult reset() noexcept
	{
		++resets;
		return CommandResult::Executed;
	}

	CommandResult configure(const Configure& request) noexcept
	{
		settings = request.settings;
		return CommandResult::Executed;
	}

	CommandResult select(Query request) noexcept
	{
		return request.channel < 3 ? CommandResult::Executed : CommandResult::InvalidValue;
	}

	Snapshot sample() const noexcept
	{
		return {voltage, settings.mode};
	}

	telemetry::ServiceResult<Snapshot> query(const Query& request) const noexcept
	{
		if (request.channel >= 3)
			return telemetry::ServiceResult<Snapshot>::failure(ServiceStatus::InvalidArgument);
		return telemetry::ServiceResult<Snapshot>::successFrom([this]() noexcept -> Snapshot {
			return sample();
		});
	}

	telemetry::BorrowedServiceResult<Settings> live(const Query& request) const noexcept
	{
		if (busy)
			return telemetry::BorrowedServiceResult<Settings>::failure(ServiceStatus::Busy);
		if (request.channel >= 3)
			return telemetry::BorrowedServiceResult<Settings>::failure(
			    ServiceStatus::InvalidArgument);
		return telemetry::BorrowedServiceResult<Settings>::success(settings);
	}

	void ping() noexcept
	{
		++resets;
	}

	telemetry::ServiceResult<void> validate(Query request) const noexcept
	{
		if (request.channel >= 3)
			return telemetry::ServiceResult<void>::failure(ServiceStatus::InvalidArgument);
		return telemetry::ServiceResult<void>::success();
	}

	const Block& echoBlock(const Block& request) const noexcept
	{
		return request;
	}
};

inline Device device;
inline const Device fixedDevice;
inline std::uint32_t freeValue = 7;

std::uint32_t readFree() noexcept
{
	return freeValue;
}

WriteResult writeFree(std::uint32_t value) noexcept
{
	freeValue = value;
	return WriteResult::Applied;
}

// Public methods:
// - operator()(): Read owner voltage.
struct Getter {
	Device* owner;

	float operator()() const noexcept
	{
		return owner->readVoltage();
	}
};

inline Getter functor{&device};
inline auto closure = [owner = &device]() noexcept -> float {
	return owner->readVoltage();
};

inline telemetry::OwnerSlot<Device> ownerSlot;
inline telemetry::FunctionSlot<float() noexcept> functionSlot;
inline telemetry::ContextFunctionSlot<float() noexcept> contextSlot;
inline telemetry::DelegateRefSlot<float() noexcept> referenceSlot;
inline telemetry::DelegateSlot<float() noexcept, 32> ownedSlot;

enum class Position : unsigned {
	Voltage,
	SettingsCopy,
	SettingsView,
	BlockView,
	Limits
};
inline constexpr telemetry::FieldTable localFields{
    telemetry::field<&Device::readVoltage, &Device::writeVoltage>("Voltage", device),
    telemetry::field<&Device::readSettings, &Device::writeSettings>("SettingsCopy", device),
    telemetry::field<&Device::borrowSettings>("SettingsView", device),
    telemetry::field<&Device::readBlock, &Device::writeBlock>("BlockView", device),
    telemetry::field<&Device::readLimits>("Limits", fixedDevice),
};
inline constexpr telemetry::FieldTable callbackFields{
    telemetry::field<&readFree, &writeFree>("Free"),
    telemetry::field("RuntimeFunction", &readFree),
    telemetry::field("Stateless",
                     []() noexcept -> std::uint32_t {
	                     return 42;
                     }),
    telemetry::field("Functor", functor),
    telemetry::field("Closure", closure),
    telemetry::field<&Device::readVoltage>("OwnerSlot", ownerSlot),
    telemetry::field("FunctionSlot", functionSlot),
    telemetry::field("ContextSlot", contextSlot),
    telemetry::field("DelegateRefSlot", referenceSlot),
    telemetry::field("DelegateSlot", ownedSlot),
};
inline constexpr telemetry::CommandTable localCommands{
    telemetry::command<&Device::reset>("Reset", device),
    telemetry::command<&Device::configure>("Configure", device),
    telemetry::command<&Device::select>("Select", device),
};
inline constexpr telemetry::ServiceTable localServices{
    telemetry::service<&Device::sample>("Sample", device),
    telemetry::service<&Device::query>("Query", device),
    telemetry::service<&Device::borrowSettings>("Settings", device),
    telemetry::service<&Device::live>("Live", device),
    telemetry::service<&Device::ping>("Ping", device),
    telemetry::service<&Device::validate>("Validate", device),
    telemetry::service<&Device::echoBlock>("EchoBlock", device),
};
inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("meter", localFields),
                                                     telemetry::group("callbacks", callbackFields)};
inline constexpr telemetry::CommandCatalogTable commands{
    telemetry::group("control", localCommands)};
inline constexpr telemetry::ServiceCatalogTable services{
    telemetry::group("queries", localServices)};
inline constexpr telemetry::Model model{fields, commands, services};

static_assert(telemetry::reflection::memberCount<Settings> == 3);
static_assert(telemetry::reflection::memberName<0, Settings>() == "calibration");
static_assert(telemetry::reflection::Enum<Mode>::entryCount == 3);
static_assert(telemetry::reflection::Enum<Label>::entryName<1>() == "Віддалений");
static_assert(telemetry::wireSize<Snapshot> == sizeof(float) + sizeof(std::int16_t));
static_assert(
    std::is_same_v<decltype(localFields.read<Position::SettingsCopy>()), std::optional<Settings>>);
static_assert(std::is_same_v<decltype(localFields.read<Position::SettingsView>()),
                             telemetry::BorrowedValue<Settings>>);
static_assert(
    std::is_same_v<decltype(localServices.call<2>()), telemetry::BorrowedServiceResult<Settings>>);
static_assert(model.maxFieldScratch() >= telemetry::scratchBytes<Block>);
static_assert(model.typeId<Calibration>() != model.typeId<Settings>());

void demonstrate()
{
	const auto referenceWrapperService =
	    telemetry::service<&Device::sample>("Reference wrapper", std::cref(device));
	assert(referenceWrapperService.call().value().voltage == device.voltage);
	assert(!callbackFields.read<5>() && !callbackFields.read<6>());
	ownerSlot.bind(device);
	functionSlot.bind(+[]() noexcept -> float {
		return 12.0f;
	});
	contextSlot.bind(
	    +[](void* context) noexcept -> float {
		    return context == nullptr ? 0.0f : static_cast<Device*>(context)->readVoltage();
	    },
	    &device);
	referenceSlot.bind<&Device::readVoltage>(device);
	ownedSlot.bind([bias = 0.5f, owner = &device]() noexcept -> float {
		return owner->readVoltage() + bias;
	});
	assert(callbackFields.read<5>() == device.voltage);
	assert(callbackFields.read<6>() == 12.0f);
	assert(callbackFields.read<7>() == device.voltage);
	assert(callbackFields.read<8>() == device.voltage);
	assert(callbackFields.read<9>() == device.voltage + 0.5f);

	assert(localFields.write<Position::Voltage>(240.0f) == WriteResult::Applied);
	assert((localFields.readAs<double, Position::Voltage>() == 240.0));
	constexpr auto voltageId = telemetry::makeId<0, Position::Voltage>();
	assert(telemetry::groupOf(voltageId) == 0 && telemetry::indexOf(voltageId) == 0);
	assert(telemetry::tryGroupOf(voltageId) == 0 && telemetry::tryIndexOf(voltageId) == 0);
	assert(!telemetry::tryGroupOf(-1) && !telemetry::tryIndexOf(std::uint64_t{1} << 32));
	assert((fields.read<voltageId>() == 240.0f));
	assert(fields.readAs<double>(voltageId) == 240.0);
	assert(fields.writeAs(voltageId, 241) == WriteResult::Applied);
	assert(fields.writeAs(voltageId, 400) == WriteResult::InvalidValue);
	assert(fields.writeAs(telemetry::makeId<9, 0>(), 1) == WriteResult::NotFound);
	assert(!fields.readAs<double>(-1));

	const auto view = localFields.read<Position::SettingsView>();
	const auto copy = localFields.readAs<Settings, Position::SettingsView>();
	assert(view && copy && view.valueOrNull() == &device.settings);
	assert(localCommands.call<0>() == CommandResult::Executed);
	assert((commands.call<telemetry::makeId<0, 1>()>(Configure{device.settings}) ==
	        CommandResult::Executed));
	assert(localCommands.call<2>(Query{9}) == CommandResult::InvalidValue);
	const auto response = services.call<telemetry::makeId<0, 1>()>(Query{0});
	assert(response.hasValue() && response.value().voltage == device.voltage);
	const auto borrowedResponse = localServices.call<3>(Query{0});
	assert(borrowedResponse && borrowedResponse.valueOrNull() == &device.settings);
	device.busy = true;
	assert(localServices.call<3>(Query{0}).status() == ServiceStatus::Busy);
	device.busy = false;
	assert(localServices.call<4>().status() == ServiceStatus::Ok);
	assert(localServices.call<5>(Query{9}).status() == ServiceStatus::InvalidArgument);

	unsigned nativeCount = 0;
	localFields.forEach([&]<std::size_t I>(const auto& definition) {
		assert(I == nativeCount && definition.name() != nullptr);
		++nativeCount;
	});
	unsigned globalCount = 0;
	fields.forEach(
	    [&]<std::size_t G, std::size_t I>(std::string_view name, const auto& definition) {
		    assert(name == fields[G].name && definition.name() == fields[G].entries[I].name);
		    ++globalCount;
	    });
	assert(nativeCount == localFields.size() &&
	       globalCount == localFields.size() + callbackFields.size());
	assert(fields.visit(voltageId, [](const auto& definition) {
		assert(definition.name() != nullptr);
	}));
	assert(!fields.visit(telemetry::makeId<9, 0>(), [](const auto&) {
	}));
	auto request = Query{0};
	CommandResult commandStatus = CommandResult::NotFound;
	const bool selected = commands.visit(telemetry::makeId<0, 2>(), [&](const auto& definition) {
		using Definition = std::remove_cvref_t<decltype(definition)>;
		if constexpr (std::is_same_v<typename Definition::Request, Query>)
			commandStatus = definition.call(request);
	});
	assert(selected && commandStatus == CommandResult::Executed);
	for (const auto& catalog : fields)
		for (std::uint32_t i = 0; i < catalog.count; ++i)
			assert(catalog.entries[i].name != nullptr);
	const auto types = model.types();
	for (std::uint32_t i = 0; i < types.count; ++i)
		assert(types.find(i) != nullptr);
	assert(model.view().fieldTypeId(voltageId) == model.typeId<float>());

	std::array<std::byte, model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, telemetry::wireSize<float>> voltageWire{};
	assert(fields.index().readEncoded(voltageId, voltageWire, workspace).dispatch ==
	       telemetry::DispatchStatus::Ok);
	const float replacement = 242.0f;
	assert(telemetry::encode(replacement, voltageWire) == telemetry::CodecStatus::Ok);
	assert(fields.index().writeEncoded(voltageId, voltageWire, workspace).endpointStatus ==
	       WriteResult::Applied);
	std::array<std::byte, telemetry::wireSize<Query>> queryWire{};
	std::array<std::byte, telemetry::wireSize<Snapshot>> snapshotWire{};
	assert(telemetry::encode(Query{0}, queryWire) == telemetry::CodecStatus::Ok);
	{
		auto lease = workspace.reserve<Query>();
		Query* decoded = nullptr;
		assert(telemetry::decode<Query>(queryWire, lease, decoded) == telemetry::CodecStatus::Ok);
		assert(decoded != nullptr && decoded->channel == 0);
		assert(telemetry::decode<Query>(std::span<const std::byte>{queryWire}.first(1), lease,
		                                decoded) == telemetry::CodecStatus::LengthMismatch);
		assert(decoded == nullptr); // Failure clears the pointer; lease owns the first object.
	}
	assert(workspace.used() == 0);
	const auto encoded =
	    services.index().callEncoded(telemetry::makeId<0, 1>(), queryWire, snapshotWire, workspace);
	assert(encoded.dispatch == telemetry::DispatchStatus::Ok &&
	       encoded.endpointStatus == ServiceStatus::Ok);
	assert(encoded.written == snapshotWire.size());
	assert(commands.index()
	           .executeEncoded(telemetry::makeId<0, 2>(), queryWire, workspace)
	           .endpointStatus == CommandResult::Executed);
	assert(workspace.used() == 0);
	const auto blockEntry = fields.index().find(telemetry::makeId<0, Position::BlockView>());
	assert(blockEntry != nullptr && blockEntry->readScratchBytes == 0);
	assert(blockEntry->writeScratchBytes == telemetry::scratchBytes<Block>);

	ownerSlot.reset();
	functionSlot.reset();
	contextSlot.reset();
	referenceSlot.reset();
	ownedSlot.reset();
	assert(!callbackFields.read<5>() && !callbackFields.read<9>());
}
} // namespace guide

int main()
{
	guide::demonstrate();
	std::puts("Native API guide: examples passed");
}
