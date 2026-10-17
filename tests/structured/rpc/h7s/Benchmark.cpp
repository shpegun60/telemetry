/*
 * @file Benchmark.cpp
 * @brief Actual UART Resource packets with external fixture session controls.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <resource/Resource.hpp>
#include <resource/protocol/Protocol.hpp>
#include <resource/telemetry/v3/RpcMailbox.hpp>
#include "main.h"
#include "usart.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace {
namespace ts = telemetry;
namespace rpc = resource::telemetry::v3;
constexpr std::size_t frameCapacity = 256;
constexpr std::size_t metadataBytes = 9;
constexpr std::uint32_t admittedPeer = 1;
std::uint32_t generation = 0, rejected = 0, packets = 0, acknowledgements = 0, queueDrops = 0;
bool executionHeld = false, writeAckComplete = true;

std::uint32_t load32(const std::byte* p) noexcept
{
	std::uint32_t result = 0;
	for (unsigned i = 0; i < 4; ++i)
		result |= std::uint32_t(std::to_integer<unsigned>(p[i])) << (8 * i);
	return result;
}

void store32(std::byte* p, std::uint32_t value) noexcept
{
	for (unsigned i = 0; i < 4; ++i)
		p[i] = std::byte(value >> (8 * i));
}

struct Request {
	std::uint32_t delta;
	std::array<std::uint8_t, 64> bytes;
};

struct Response {
	std::uint32_t value, calls;
	std::array<std::uint8_t, 192> bytes;
};

struct Device {
	std::uint32_t calls = 0, value = 0, beforeAck = 0, commands = 0;

	ts::CommandResult increment() noexcept
	{
		if (!writeAckComplete)
			++beforeAck;
		++commands;
		++value;
		return ts::CommandResult::Executed;
	}

	ts::ServiceResult<Response> add(const Request& request) noexcept
	{
		if (!writeAckComplete)
			++beforeAck;
		++calls;
		value += request.delta;
		return ts::ServiceResult<Response>::successFrom([&]() noexcept {
			Response response{value, calls, {}};
			for (unsigned i = 0; i < response.bytes.size(); ++i)
				response.bytes[i] = static_cast<std::uint8_t>(i);
			return response;
		});
	}
};

Device device;
constexpr ts::ServiceTable services{ts::service<&Device::add>("Add", device)};
constexpr ts::ServiceCatalogTable groups{ts::group("fixture", services)};
constexpr ts::CommandTable commands{ts::command<&Device::increment>("Increment", device)};
constexpr ts::CommandCatalogTable commandGroups{ts::group("fixture", commands)};
constexpr ts::Model model{ts::emptyFields, commandGroups, groups};
constexpr auto view = model.view();
std::array<std::byte, 128> requestBytes;
std::array<std::byte, ts::wireSize<Response>> resultBytes;
std::array<std::byte, model.maxScratch()> scratchBytes;
ts::Workspace workspace{scratchBytes};
rpc::RpcMailbox mailbox{view, workspace, requestBytes, resultBytes};

struct Stats {
	resource::FileSize size() const noexcept
	{
		return 44;
	}

	resource::ReadResult read(resource::Cursor cursor, resource::Output output) const noexcept
	{
		if (cursor > size())
			return {resource::Status::InvalidCursor, cursor};
		if (cursor != size() && output.empty())
			return {resource::Status::BufferTooSmall, cursor};
		std::array<std::byte, 44> bytes{};
		const std::array<std::uint32_t, 11> words{0x53504352,   SystemCoreClock,  device.calls,
		                                          device.value, device.beforeAck, generation,
		                                          rejected,     packets,          acknowledgements,
		                                          queueDrops,   device.commands};
		for (unsigned i = 0; i < words.size(); ++i)
			store32(bytes.data() + 4 * i, words[i]);
		const auto count = std::min(output.size(), static_cast<std::size_t>(size() - cursor));
		std::copy_n(bytes.data() + cursor, count, output.data());
		return {resource::Status::Ok, cursor + count, static_cast<std::uint32_t>(count),
		        cursor + count == size()};
	}
};

Stats stats;
const auto files = resource::filesystem(resource::file("/telemetry/rpc.bin", mailbox),
                                        resource::file("/fixture/stats.bin", stats));

struct Frame {
	std::array<std::byte, frameCapacity> bytes{};
	std::uint16_t size = 0;
};

std::array<Frame, 4> queue;
unsigned head = 0, tail = 0, queued = 0;
std::array<std::byte, 128> reply;

void discardPartial() noexcept
{
	std::uint8_t ignored;
	for (unsigned i = 0; i < frameCapacity; ++i)
		if (HAL_UART_Receive(&huart3, &ignored, 1, 10) != HAL_OK)
			break;
}

bool receive() noexcept
{
	std::array<std::uint8_t, 2> prefix{};
	if (HAL_UART_Receive(&huart3, prefix.data(), prefix.size(), 20) != HAL_OK) {
		if (huart3.RxXferCount != prefix.size())
			discardPartial();
		return false;
	}
	const auto size = std::uint16_t(prefix[0] | (unsigned(prefix[1]) << 8));
	if (size < metadataBytes || size > frameCapacity) {
		discardPartial();
		return false;
	}
	Frame incoming;
	incoming.size = size;
	if (HAL_UART_Receive(&huart3, reinterpret_cast<std::uint8_t*>(incoming.bytes.data()), size,
	                     200) != HAL_OK) {
		discardPartial();
		return false;
	}
	if (queued == queue.size()) {
		++queueDrops;
		return false;
	}
	queue[tail] = incoming;
	tail = (tail + 1) % queue.size();
	++queued;
	return true;
}

bool transmit(std::byte kind, std::uint32_t peer, std::uint32_t epoch,
              resource::Input payload) noexcept
{
	std::array<std::byte, 2 + metadataBytes + 128> bytes{};
	const auto size = metadataBytes + payload.size();
	bytes[0] = std::byte(size);
	bytes[1] = std::byte(size >> 8);
	bytes[2] = kind;
	store32(bytes.data() + 3, peer);
	store32(bytes.data() + 7, epoch);
	std::copy(payload.begin(), payload.end(), bytes.begin() + 11);
	return HAL_UART_Transmit(&huart3, reinterpret_cast<const std::uint8_t*>(bytes.data()),
	                         static_cast<std::uint16_t>(size + 2), 1000) == HAL_OK;
}

void control(std::byte kind, std::uint32_t peer, std::uint32_t epoch, unsigned status) noexcept
{
	const std::array<std::byte, 1> payload{std::byte(status)};
	(void)transmit(kind, peer, epoch, payload);
}

void consume() noexcept
{
	if (!queued)
		return;
	const Frame frame = queue[head];
	head = (head + 1) % queue.size();
	--queued;
	const auto kind = std::to_integer<unsigned>(frame.bytes[0]);
	const auto peer = load32(frame.bytes.data() + 1), epoch = load32(frame.bytes.data() + 5);
	const resource::Input payload{frame.bytes.data() + metadataBytes, frame.size - metadataBytes};
	// Kind 3 is a fixture-only application disconnect/new-session event.
	if (kind == 3 && peer == admittedPeer && epoch > generation && payload.empty()) {
		mailbox.resetSession();
		generation = epoch;
		queued = head = tail = 0;
		executionHeld = false;
		writeAckComplete = true;
		control(std::byte{3}, peer, epoch, 0);
		return;
	}
	if (peer != admittedPeer || epoch != generation || !generation) {
		++rejected;
		control(std::byte{127}, peer, epoch, peer != admittedPeer ? 1 : 2);
		return;
	}
	if (kind == 2 && payload.size() == 1 && std::to_integer<unsigned>(payload[0]) <= 1) {
		executionHeld = payload[0] != std::byte{};
		control(std::byte{2}, peer, epoch, 0);
		return;
	}
	if (kind != 1) {
		++rejected;
		control(std::byte{127}, peer, epoch, 3);
		return;
	}
	++packets;
	// Receive can wait; a newly consumed WRITE needs the current owner tick.
	// A held Pending request remains behind its successful final ACK.
	if (mailbox.state() != rpc::RpcState::Pending)
		mailbox.process(HAL_GetTick());
	const bool command =
	    payload.size() >= 5 && payload[0] == std::byte{4} && load32(payload.data() + 1) == 0;
	const bool oldAck = writeAckComplete;
	if (command)
		writeAckComplete = false;
	const auto result = resource::protocol::process(files.view(), payload, reply);
	const bool sent =
	    transmit(std::byte{1}, peer, epoch, resource::Input{reply}.first(result.written));
	const bool complete = command && result.status == resource::Status::Ok &&
	                      result.written == 14 && reply[13] == std::byte{1};
	if (complete) {
		writeAckComplete = sent;
		if (sent)
			++acknowledgements;
	} else if (command)
		writeAckComplete = oldAck;
}
} // namespace

extern "C" void bench_init()
{
	MPU_Region_InitTypeDef region{};
	HAL_MPU_Disable();
	region.Enable = MPU_REGION_ENABLE;
	region.Number = MPU_REGION_NUMBER1;
	region.BaseAddress = 0x24000000;
	region.Size = MPU_REGION_SIZE_512KB;
	region.AccessPermission = MPU_REGION_FULL_ACCESS;
	region.TypeExtField = MPU_TEX_LEVEL1;
	region.IsCacheable = MPU_ACCESS_CACHEABLE;
	region.IsBufferable = MPU_ACCESS_BUFFERABLE;
	region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
	region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
	HAL_MPU_ConfigRegion(&region);
	region.Number = MPU_REGION_NUMBER2;
	region.BaseAddress = 0x25000000;
	region.Size = MPU_REGION_SIZE_16MB;
	region.AccessPermission = MPU_REGION_NO_ACCESS;
	region.TypeExtField = MPU_TEX_LEVEL0;
	region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
	region.IsBufferable = MPU_ACCESS_BUFFERABLE;
	region.IsShareable = MPU_ACCESS_SHAREABLE;
	HAL_MPU_ConfigRegion(&region);
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
	SCB_EnableICache();
	SCB_EnableDCache();
}

extern "C" void bench_loop()
{
	// A completed callback returns before another packet receives a fresh tick.
	if (mailbox.state() == rpc::RpcState::Pending) {
		if (!executionHeld && writeAckComplete) {
			mailbox.process(HAL_GetTick());
			return;
		}
	} else
		mailbox.process(HAL_GetTick());
	(void)receive();
	for (unsigned i = 1; i < queue.size() && __HAL_UART_GET_FLAG(&huart3, UART_FLAG_RXNE); ++i)
		(void)receive();
	consume();
}
