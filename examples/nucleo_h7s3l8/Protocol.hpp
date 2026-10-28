/**
 * @file Protocol.hpp
 * @brief Read-only synchronous demonstration Model and native protocol dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_PROTOCOL_HPP
#define TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_PROTOCOL_HPP
#pragma once
#include <integration/encoded/Dispatcher.hpp>
#include <integration/telemetry/v3/DescriptorFile.hpp>
#include <resource/BytesFile.hpp>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <telemetry/Telemetry.hpp>
#include <telemetry/descriptor/Descriptor.hpp>
#include <array>
#include <cstdint>

namespace nucleo_demo {
struct Voltage {
	float Ua, Ub, Uc;
};
enum class Mode : std::int16_t {
	Idle = -7,
	Active = 12
};

struct Probe {
	std::array<std::array<std::int32_t, 3>, 2> Matrix;
	bool Enabled;
	Mode mode;
};

struct Status {
	std::uint32_t uptimeMs, requests, fieldReads, refused;
	bool demonstration;
};

inline Status status{0, 0, 0, 0, true};

inline Voltage voltage() noexcept
{
	return {230.1f, 229.8f, 231.2f};
}

inline std::uint64_t counter() noexcept
{
	return UINT64_MAX;
}

inline const Status& readStatus() noexcept
{
	return status;
}

inline Probe probe() noexcept
{
	return {{{{1, -2, 3}, {4, 5, -6}}}, true, static_cast<Mode>(-2)};
}
} // namespace nucleo_demo

namespace telemetry::reflection {
template<>
struct EnumReflection<nucleo_demo::Mode> {
	inline static constexpr auto entries = enumEntries(
	    enumEntry(nucleo_demo::Mode::Idle, "Idle"), enumEntry(nucleo_demo::Mode::Active, "Active"));
};
} // namespace telemetry::reflection

namespace nucleo_demo {
inline constexpr telemetry::FieldTable measureFields{telemetry::field<&voltage>("Voltage")};
inline constexpr telemetry::FieldTable deviceFields{telemetry::field<&counter>("Counter"),
                                                    telemetry::field<&readStatus>("Status"),
                                                    telemetry::field<&probe>("Probe")};
inline constexpr telemetry::FieldCatalogTable fields{telemetry::group("measure", measureFields),
                                                     telemetry::group("device", deviceFields)};
inline constexpr telemetry::CommandCatalogTable<> commands{};
inline constexpr telemetry::ServiceCatalogTable<> services{};
inline constexpr telemetry::Model model{fields, commands, services};
inline constexpr telemetry::descriptor::Descriptor descriptor{model};
inline constexpr auto packedDescriptor = telemetry::descriptor::packDescriptor<descriptor>();
inline constexpr resource::telemetry::v3::DescriptorFile descriptorFile{packedDescriptor};
inline constexpr std::array<std::byte, 4> versionBytes{std::byte{1}, std::byte{0}, std::byte{0},
                                                       std::byte{0}};
inline constexpr resource::BytesFile versionFile{versionBytes};
inline constexpr auto files =
    resource::filesystem(resource::file("/device/version.bin", versionFile),
                         resource::file("/telemetry/descriptor.bin", descriptorFile));

class Protocol {
public:
	[[nodiscard]] std::size_t dispatch(resource::Input input, resource::Output output,
	                                   std::uint32_t now) noexcept
	{
		status.uptimeMs = now;
		++status.requests;
		if (input.empty())
			return refuse();
		const auto code = std::to_integer<unsigned>(input[0]);
		if (code >= 1 && code <= 3) {
			const auto result = resource::protocol::process(files.view(), input, output);
			return result.written;
		}
		const auto request = telemetry::protocol::encoded::parse(input);
		if (!request || request.header.reply ||
		    request.header.operation != telemetry::protocol::encoded::Operation::FieldRead)
			return refuse();
		++status.fieldReads;
		// No remote session state or mailbox: every read is synchronous. The
		// exclusive host supplies and correlates a fresh nonzero TEQ1 session.
		const auto result = telemetry::integration::encoded::dispatchEncoded(
		    model.view(), workspace_, request.header.session, input, output);
		return result ? result.written : refuse();
	}

private:
	std::size_t refuse() noexcept
	{
		++status.refused;
		return 0;
	}

	alignas(std::max_align_t) std::array<std::byte, model.maxScratch()> scratch_{};
	telemetry::Workspace workspace_{scratch_};
};
} // namespace nucleo_demo
#endif // TELEMETRY_EXAMPLES_NUCLEO_H7S3L8_PROTOCOL_HPP
