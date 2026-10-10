/*
 * @file ArmConvenience.cpp
 * @brief Cortex-M7 existing-object and scoped decode frames for 1 KiB and 4 KiB DTOs.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

template<std::size_t Bytes>
struct Big {
	std::array<std::uint32_t, Bytes / sizeof(std::uint32_t)> values;
};

using Small = Big<1024>;
using Large = Big<4096>;
static_assert(sizeof(Small) == 1024 && sizeof(Large) == 4096);

// Separate noinline exports make the entire reservation/decode/release wrapper
// visible in .su and disassembly rather than measuring a leaf helper alone.
extern "C" [[gnu::noinline]] telemetry::CodecStatus
convenience_into_1024(std::span<const std::byte> input, Small& output) noexcept
{
	return telemetry::decode(input, output);
}

extern "C" [[gnu::noinline]] telemetry::CodecStatus
convenience_into_4096(std::span<const std::byte> input, Large& output) noexcept
{
	return telemetry::decode(input, output);
}

extern "C" [[gnu::noinline]] std::uint32_t
convenience_legacy_1024(std::span<const std::byte> input, telemetry::Workspace& workspace) noexcept
{
	auto lease = workspace.reserve<Small>();
	Small* output = nullptr;
	if (telemetry::decode(input, lease, output) != telemetry::CodecStatus::Ok)
		return 0;
	return output->values.front() + output->values.back();
}

extern "C" [[gnu::noinline]] std::uint32_t
convenience_legacy_4096(std::span<const std::byte> input, telemetry::Workspace& workspace) noexcept
{
	auto lease = workspace.reserve<Large>();
	Large* output = nullptr;
	if (telemetry::decode(input, lease, output) != telemetry::CodecStatus::Ok)
		return 0;
	return output->values.front() + output->values.back();
}

extern "C" [[gnu::noinline]] std::uint32_t
convenience_scoped_1024(std::span<const std::byte> input, telemetry::Workspace& workspace) noexcept
{
	auto decoded = telemetry::decode<Small>(input, workspace);
	if (!decoded)
		return 0;
	return decoded.value().values.front() + decoded.value().values.back();
}

extern "C" [[gnu::noinline]] std::uint32_t
convenience_scoped_4096(std::span<const std::byte> input, telemetry::Workspace& workspace) noexcept
{
	auto decoded = telemetry::decode<Large>(input, workspace);
	if (!decoded)
		return 0;
	return decoded.value().values.front() + decoded.value().values.back();
}

extern "C" [[gnu::noinline]] telemetry::Decoded<Small>*
convenience_placement_1024(void* storage, std::span<const std::byte> input,
                           telemetry::Workspace& workspace) noexcept
{
	return ::new (storage) telemetry::Decoded<Small>(telemetry::decode<Small>(input, workspace));
}

extern "C" [[gnu::noinline]] telemetry::Decoded<Large>*
convenience_placement_4096(void* storage, std::span<const std::byte> input,
                           telemetry::Workspace& workspace) noexcept
{
	return ::new (storage) telemetry::Decoded<Large>(telemetry::decode<Large>(input, workspace));
}
