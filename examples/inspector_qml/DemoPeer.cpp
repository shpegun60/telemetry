/*
 * @file DemoPeer.cpp
 * @brief Real native Model, Descriptor and Encoded dispatcher for the Qt demo.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "DemoPeer.hpp"

#include <integration/encoded/Dispatcher.hpp>
#include <integration/tcp/Server.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>

#include <array>
#include <limits>

namespace inspector_qml::demo {

struct Voltage {
	float Ua, Ub, Uc;
};

enum class Mode : std::int16_t {
	Idle = -7,
	Active = 12
};

struct Empty {};

struct Nested {
	std::array<std::array<std::int32_t, 3>, 2> Matrix;
	std::array<Voltage, 2> Voltages;
	bool Enabled;
};

struct Calibration {
	float reference;
};

struct Identity {
	std::uint32_t version, serial;
};

struct Device {
	Voltage voltage() const noexcept
	{
		return {230.1f, 229.8f, 231.2f};
	}

	std::uint64_t counter() const noexcept
	{
		return std::numeric_limits<std::uint64_t>::max();
	}

	std::int64_t offset() const noexcept
	{
		return std::numeric_limits<std::int64_t>::min();
	}

	Mode mode() const noexcept
	{
		return static_cast<Mode>(-2);
	}

	std::array<std::uint16_t, 16384> samples() const noexcept
	{
		std::array<std::uint16_t, 16384> values{};
		for (std::size_t i = 0; i < values.size(); ++i)
			values[i] = static_cast<std::uint16_t>(i);
		return values;
	}

	Empty empty() const noexcept
	{
		return {};
	}

	Nested nested() const noexcept
	{
		return {{{{1, -2, 3}, {4, 5, -6}}}, {{{1.25f, 2.5f, 3.75f}, {4.5f, 5.25f, 6.0f}}}, true};
	}

	telemetry::CommandResult calibrate(const Calibration&) const noexcept
	{
		return telemetry::CommandResult::Executed;
	}

	telemetry::ServiceResult<Identity> identity() const noexcept
	{
		return telemetry::ServiceResult<Identity>::success({1, 42});
	}
};

} // namespace inspector_qml::demo

namespace telemetry::reflection {

template<>
struct EnumReflection<inspector_qml::demo::Mode> {
	inline static constexpr auto entries =
	    enumEntries(enumEntry(inspector_qml::demo::Mode::Idle, "Idle"),
	                enumEntry(inspector_qml::demo::Mode::Active, "Active"));
};

} // namespace telemetry::reflection

namespace inspector_qml {
namespace {

demo::Device device;
constexpr telemetry::FieldTable measureFields{
    telemetry::field<&demo::Device::voltage>("Voltage", device)};
constexpr telemetry::FieldTable deviceFields{
    telemetry::field<&demo::Device::counter>("Counter", device),
    telemetry::field<&demo::Device::offset>("Offset", device),
    telemetry::field<&demo::Device::mode>("Mode", device)};
constexpr telemetry::FieldTable dataFields{
    telemetry::field<&demo::Device::samples>("Samples", device),
    telemetry::field<&demo::Device::empty>("Empty", device),
    telemetry::field<&demo::Device::nested>("Nested", device)};
constexpr telemetry::FieldCatalogTable fields{telemetry::group("measure", measureFields),
                                              telemetry::group("device", deviceFields),
                                              telemetry::group("data", dataFields)};
constexpr telemetry::CommandTable controlCommands{
    telemetry::command<&demo::Device::calibrate>("Calibrate", device)};
constexpr telemetry::CommandCatalogTable commands{telemetry::group("control", controlCommands)};
constexpr telemetry::ServiceTable deviceServices{
    telemetry::service<&demo::Device::identity>("Identity", device)};
constexpr telemetry::ServiceCatalogTable services{telemetry::group("device", deviceServices)};
constexpr telemetry::Model model{fields, commands, services};
constexpr telemetry::descriptor::Descriptor nativeDescriptor{model};
constexpr auto descriptorBytes = telemetry::descriptor::packDescriptor<nativeDescriptor>();

} // namespace

struct DemoPeer::Impl {
	alignas(std::max_align_t) std::array<std::byte, model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
};

DemoPeer::DemoPeer() : impl_(std::make_unique<Impl>())
{}

DemoPeer::~DemoPeer() = default;

telemetry::client::Input DemoPeer::descriptor() const noexcept
{
	return descriptorBytes;
}

std::unique_ptr<telemetry::integration::tcp::Server>
DemoPeer::makeTcpServer(resource::FileSystemView files, telemetry::client::Output rx,
                        telemetry::client::Output tx)
{
	return std::make_unique<telemetry::integration::tcp::Server>(model.view(), impl_->workspace,
	                                                             files, rx, tx);
}

std::size_t DemoPeer::dispatch(telemetry::client::Input request, telemetry::client::Output reply,
                               std::uint32_t session) noexcept
{
	const auto result = telemetry::integration::encoded::dispatchEncoded(
	    model.view(), impl_->workspace, session, request, reply);
	return result ? result.written : 0;
}

} // namespace inspector_qml
