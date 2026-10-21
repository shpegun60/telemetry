/*
 * @file WriterNative.cpp
 * @brief Compare dynamic edits with Native bytes and actual Field write/read dispatch.
 * SPDX-License-Identifier: MIT
 */
#include "Fixture.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
unsigned checks = 0;

void require(bool condition, int line)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "runtime writer native check %u failed at line %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) require(bool(__VA_ARGS__), __LINE__)
} // namespace

int main(int argc, char** argv)
{
	using namespace runtime_fixture;
	if (argc != 2)
		return 2;
	std::array<std::byte, telemetry::wireSize<Packet>> dynamic{}, expected{}, readback{};
	auto* file = std::fopen(argv[1], "rb");
	CHECK(file);
	CHECK(std::fread(dynamic.data(), 1, dynamic.size(), file) == dynamic.size());
	CHECK(std::fgetc(file) == EOF);
	CHECK(std::fclose(file) == 0);
	auto edited = initial;
	edited.flag = false;
	edited.u32 = 115200;
	edited.f32 = std::bit_cast<float>(0x7fc12345u);
	edited.nested.mode = static_cast<Mode>(-2);
	CHECK(telemetry::encode(edited, expected) == telemetry::CodecStatus::Ok);
	for (std::size_t i = 0; i < dynamic.size(); ++i)
		CHECK(dynamic[i] == expected[i]);
	Packet decoded{};
	CHECK(telemetry::decode(dynamic, decoded) == telemetry::CodecStatus::Ok);
	CHECK(!decoded.flag);
	CHECK(decoded.u32 == 115200);
	CHECK(std::bit_cast<std::uint32_t>(decoded.f32) == 0x7fc12345u);
	CHECK(static_cast<std::int16_t>(decoded.nested.mode) == -2);
	alignas(Packet) std::array<std::byte, sizeof(Packet) + alignof(Packet)> scratch{};
	telemetry::Workspace workspace{scratch};
	const auto write = telemetry::dispatchEncoded(model.view(), telemetry::Operation::FieldWrite, 0,
	                                              dynamic, {}, workspace);
	CHECK(write.dispatch == telemetry::DispatchStatus::Ok);
	CHECK(write.endpointStatus == static_cast<std::uint8_t>(telemetry::WriteResult::Applied));
	CHECK(write.written == 0);
	CHECK(device.packet.u32 == 115200);
	CHECK(std::bit_cast<std::uint32_t>(device.packet.f32) == 0x7fc12345u);
	const auto read = telemetry::dispatchEncoded(model.view(), telemetry::Operation::FieldRead, 0,
	                                             {}, readback, workspace);
	CHECK(read.dispatch == telemetry::DispatchStatus::Ok);
	CHECK(read.endpointStatus == 0);
	CHECK(read.written == readback.size());
	CHECK(device.reads == 1);
	for (std::size_t i = 0; i < dynamic.size(); ++i)
		CHECK(readback[i] == dynamic[i]);
	std::printf("runtime writer native: %u checks, 0 failures\n", checks);
}
