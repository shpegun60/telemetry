/*
 * @file Convenience.cpp
 * @brief Existing-object and scoped-workspace decode validation and lifetime checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include <telemetry/codec/Codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Any allocation in a decode path immediately fails the probe.
#if !defined(__arm__)
void* operator new(std::size_t)
{
	std::abort();
}

void* operator new[](std::size_t)
{
	std::abort();
}

void* operator new(std::size_t, std::align_val_t)
{
	std::abort();
}

void* operator new[](std::size_t, std::align_val_t)
{
	std::abort();
}

void operator delete(void*) noexcept
{
	std::abort();
}

void operator delete[](void*) noexcept
{
	std::abort();
}

void operator delete(void*, std::size_t) noexcept
{
	std::abort();
}

void operator delete[](void*, std::size_t) noexcept
{
	std::abort();
}

void operator delete(void*, std::align_val_t) noexcept
{
	std::abort();
}

void operator delete[](void*, std::align_val_t) noexcept
{
	std::abort();
}

void operator delete(void*, std::size_t, std::align_val_t) noexcept
{
	std::abort();
}

void operator delete[](void*, std::size_t, std::align_val_t) noexcept
{
	std::abort();
}
#endif

namespace ts = telemetry;

namespace {
unsigned checks = 0;

void check(bool condition)
{
	++checks;
	if (!condition)
		std::abort();
}

struct Entry {
	std::uint32_t value;
	bool active;
};

// The last bool is deliberately far beyond the first decoded members.
struct Packet {
	std::uint16_t sequence;
	std::array<Entry, 2> entries;
	bool committed;
};

struct alignas(16) Aligned {
	std::array<std::uint32_t, 4> values;
};

unsigned dmiCalls = 0;

[[maybe_unused]] std::uint16_t dmiValue() noexcept
{
	++dmiCalls;
	return 999;
}

struct Defaulted {
	std::uint16_t value = dmiValue();
	bool active = true;
};

struct Empty {};

template<std::size_t Bytes>
struct Big {
	std::array<std::uint32_t, Bytes / sizeof(std::uint32_t)> values;
};

template<class Result>
concept HasPointer = requires(Result& result) { result.valueOrNull(); };

template<class T>
std::array<std::byte, sizeof(T)> snapshot(const T& object)
{
	std::array<std::byte, sizeof(T)> bytes;
	std::memcpy(bytes.data(), &object, sizeof(T));
	return bytes;
}

void checkTypes()
{
	using Decoded = ts::Decoded<Packet>;
	static_assert(!std::is_copy_constructible_v<Decoded>);
	static_assert(!std::is_move_constructible_v<Decoded>);
	static_assert(!std::is_copy_assignable_v<Decoded>);
	static_assert(!std::is_move_assignable_v<Decoded>);
	static_assert(std::same_as<decltype(std::declval<Decoded&>().value()), Packet&>);
	static_assert(std::same_as<decltype(std::declval<const Decoded&>().value()), const Packet&>);
	static_assert(!HasPointer<Decoded>);
	static_assert(!std::is_convertible_v<Decoded, bool>);
	static_assert(
	    noexcept(ts::decode(std::declval<std::span<const std::byte>>(), std::declval<Packet&>())));
	static_assert(noexcept(ts::decode<Packet>(std::declval<std::span<const std::byte>>(),
	                                          std::declval<ts::Workspace&>())));
	static_assert(noexcept(std::declval<const Decoded&>().status()));
	static_assert(noexcept(std::declval<const Decoded&>().hasValue()));
	static_assert(noexcept(std::declval<Decoded&>().value()));
}

void checkExistingObject()
{
	constexpr std::array scalar{std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
	std::uint32_t value = 7;
	check(ts::decode(scalar, value) == ts::CodecStatus::Ok);
	check(value == UINT32_C(0x12345678));
	constexpr std::array<std::byte, ts::wireSize<Packet>> golden{
	    std::byte{0x34}, std::byte{0x12}, std::byte{0x04}, std::byte{0x03}, std::byte{0x02},
	    std::byte{0x01}, std::byte{0x01}, std::byte{0x08}, std::byte{0x07}, std::byte{0x06},
	    std::byte{0x05}, std::byte{0x00}, std::byte{0x01}};
	Packet output{7, {Entry{8, false}, Entry{9, true}}, false};
	check(ts::decode(golden, output) == ts::CodecStatus::Ok);
	check(output.sequence == 0x1234);
	check(output.entries[0].value == UINT32_C(0x01020304) && output.entries[0].active);
	check(output.entries[1].value == UINT32_C(0x05060708) && !output.entries[1].active);
	check(output.committed);
	const auto before = snapshot(output);
	for (std::size_t length = 0; length < golden.size(); ++length) {
		check(ts::decode(std::span{golden}.first(length), output) ==
		      ts::CodecStatus::LengthMismatch);
		check(snapshot(output) == before);
	}
	std::array<std::byte, golden.size() + 1> longInput{};
	check(ts::decode(longInput, output) == ts::CodecStatus::LengthMismatch);
	check(snapshot(output) == before);
	for (std::size_t offset : {6u, 11u, 12u}) {
		for (std::byte invalid : {std::byte{2}, std::byte{255}}) {
			auto input = golden;
			input[offset] = invalid;
			check(ts::decode(input, output) == ts::CodecStatus::InvalidValue);
			check(snapshot(output) == before);
		}
	}
	// Object representation overlap is rejected before the first assignment.
	const auto objectBytes = std::as_bytes(std::span{&output, 1});
	check(ts::decode(objectBytes.first(ts::wireSize<Packet>), output) == ts::CodecStatus::Overlap);
	check(snapshot(output) == before);
	check(ts::decode(objectBytes.subspan(1, ts::wireSize<Packet>), output) ==
	      ts::CodecStatus::Overlap);
	check(snapshot(output) == before);
	Empty empty;
	check(ts::decode(std::span<const std::byte>{}, empty) == ts::CodecStatus::Ok);
	Defaulted defaulted{7, true};
	constexpr std::array dmiWire{std::byte{0x2a}, std::byte{0x00}, std::byte{0x00}};
	check(ts::decode(dmiWire, defaulted) == ts::CodecStatus::Ok);
	check(defaulted.value == 42 && !defaulted.active);
	check(dmiCalls == 0);
}

void checkWorkspace()
{
	Aligned source{{11, 22, 33, 44}};
	std::array<std::byte, ts::wireSize<Aligned>> wire{};
	check(ts::encode(source, wire) == ts::CodecStatus::Ok);
	alignas(Aligned) std::array<std::byte, ts::scratchBytes<Aligned> * 2 + 1> scratch{};
	ts::Workspace workspace{std::span{scratch}.subspan(1)};
	check(workspace.used() == 0);
	{
		auto result = ts::decode<Aligned>(wire, workspace);
		check(result.status() == ts::CodecStatus::Ok);
		check(result.hasValue() && static_cast<bool>(result));
		check(result.value().values == source.values);
		check(reinterpret_cast<std::uintptr_t>(&result.value()) % alignof(Aligned) == 0);
		const auto firstUsage = workspace.used();
		check(firstUsage >= sizeof(Aligned) && firstUsage <= ts::scratchBytes<Aligned>);
		{
			const auto second = ts::decode<Aligned>(wire, workspace);
			check(second.status() == ts::CodecStatus::Ok && second.hasValue());
			check(second.value().values == source.values);
			check(&second.value() != &result.value());
			check(workspace.used() > firstUsage);
		}
		check(workspace.used() == firstUsage);
		check(result.value().values == source.values);
	}
	check(workspace.used() == 0);
	{
		auto failed = ts::decode<Aligned>(std::span{wire}.first(1), workspace);
		check(failed.status() == ts::CodecStatus::LengthMismatch);
		check(!failed.hasValue() && !failed);
		check(workspace.used() != 0);
	}
	check(workspace.used() == 0);
	{
		auto failed = ts::decode<Aligned>(
		    std::span<const std::byte>{scratch.data() + 1, wire.size()}, workspace);
		check(failed.status() == ts::CodecStatus::Overlap);
		check(!failed.hasValue());
		check(workspace.used() != 0);
	}
	check(workspace.used() == 0);
	ts::Workspace small{std::span{scratch}.first(sizeof(Aligned) - 1)};
	{
		auto failed = ts::decode<Aligned>(wire, small);
		check(failed.status() == ts::CodecStatus::WorkspaceTooSmall && !failed);
		check(small.used() == 0);
	}
	check(small.used() == 0);
	constexpr std::array invalidBool{std::byte{2}};
	{
		auto failed = ts::decode<bool>(invalidBool, workspace);
		check(failed.status() == ts::CodecStatus::InvalidValue && !failed);
		check(workspace.used() != 0);
	}
	check(workspace.used() == 0);
	{
		constexpr std::array wireDmi{std::byte{0x2a}, std::byte{0x00}, std::byte{0x00}};
		const auto result = ts::decode<Defaulted>(wireDmi, workspace);
		check(result.status() == ts::CodecStatus::Ok);
		check(result.value().value == 42 && !result.value().active);
		check(dmiCalls == 0);
	}
	check(workspace.used() == 0);
	// The original explicit lease/output-pointer API is still available.
	{
		auto lease = workspace.reserve<Aligned>();
		Aligned* output = nullptr;
		check(ts::decode(wire, lease, output) == ts::CodecStatus::Ok);
		check(output != nullptr && output->values == source.values);
	}
	check(workspace.used() == 0);
	{
		auto empty = ts::decode<Empty>(std::span<const std::byte>{}, workspace);
		check(empty.status() == ts::CodecStatus::Ok && empty);
		check(workspace.used() != 0);
	}
	check(workspace.used() == 0);
}

template<std::size_t Bytes>
void checkLarge()
{
	using Payload = Big<Bytes>;
	static_assert(sizeof(Payload) == Bytes);
	static Payload source, output;
	static std::array<std::byte, ts::wireSize<Payload>> wire;
	alignas(Payload) static std::array<std::byte, ts::scratchBytes<Payload>> scratch;
	for (std::size_t i = 0; i < source.values.size(); ++i)
		source.values[i] = static_cast<std::uint32_t>(i * 131 + 7);
	check(ts::encode(source, wire) == ts::CodecStatus::Ok);
	check(ts::decode(wire, output) == ts::CodecStatus::Ok);
	for (std::size_t i = 0; i < source.values.size(); ++i)
		check(output.values[i] == source.values[i]);
	ts::Workspace workspace{scratch};
	{
		auto decoded = ts::decode<Payload>(wire, workspace);
		check(decoded.status() == ts::CodecStatus::Ok && decoded);
		check(workspace.used() == sizeof(Payload));
		for (std::size_t i = 0; i < source.values.size(); ++i)
			check(decoded.value().values[i] == source.values[i]);
	}
	check(workspace.used() == 0);
}
} // namespace

int main()
{
	checkTypes();
	checkExistingObject();
	checkWorkspace();
	checkLarge<1024>();
	checkLarge<4096>();
	std::printf("Codec convenience: %u checks passed\n", checks);
}
