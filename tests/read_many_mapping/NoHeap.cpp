/*
 * @file NoHeap.cpp
 * @brief Allocation traps around real prototype parsing, execution and ordinary Service dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "../../examples/read_many_mapping/Fixture.hpp"
#include <integration/encoded/Dispatcher.hpp>
#include <wire/EncodedWire.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
bool armed = false;
unsigned checks = 0;

void allocation() noexcept
{
	if (armed)
		std::_Exit(86);
}

void require(bool result) noexcept
{
	++checks;
	if (!result)
		std::_Exit(1);
}
#if defined(READ_MANY_WRAP_ALLOCATIONS)
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);

void* rawAllocate(std::size_t size) noexcept
{
	return __real_malloc(size);
}

void rawFree(void* pointer) noexcept
{
	__real_free(pointer);
}
#else
void* rawAllocate(std::size_t size) noexcept
{
	return std::malloc(size);
}

void rawFree(void* pointer) noexcept
{
	std::free(pointer);
}
#endif
} // namespace
#if defined(READ_MANY_WRAP_ALLOCATIONS)
extern "C" void* __wrap_malloc(std::size_t size)
{
	allocation();
	return __real_malloc(size);
}

extern "C" void* __wrap_calloc(std::size_t count, std::size_t size)
{
	allocation();
	return __real_calloc(count, size);
}

extern "C" void* __wrap_realloc(void* pointer, std::size_t size)
{
	allocation();
	return __real_realloc(pointer, size);
}

extern "C" void __wrap_free(void* pointer)
{
	allocation();
	__real_free(pointer);
}
#endif
void* operator new(std::size_t size)
{
	allocation();
	if (auto* pointer = rawAllocate(size ? size : 1))
		return pointer;
	std::abort();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void operator delete(void* pointer) noexcept
{
	if (pointer)
		allocation();
	rawFree(pointer);
}

void operator delete[](void* pointer) noexcept
{
	::operator delete(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
	::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
	::operator delete(pointer);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
	allocation();
	const auto align = static_cast<std::size_t>(alignment);
	if (align == 0 || (align & (align - 1)) != 0 || size > SIZE_MAX - (align - 1) - sizeof(void*))
		std::abort();
	auto* raw = static_cast<std::byte*>(rawAllocate(size + align - 1 + sizeof(void*)));
	if (!raw)
		std::abort();
	const auto address =
	    (reinterpret_cast<std::uintptr_t>(raw + sizeof(void*)) + align - 1) & ~(align - 1);
	auto* pointer = reinterpret_cast<void*>(address);
	static_cast<void**>(pointer)[-1] = raw;
	return pointer;
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
	return ::operator new(size, alignment);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
	if (pointer) {
		allocation();
		rawFree(static_cast<void**>(pointer)[-1]);
	}
}

void operator delete[](void* pointer, std::align_val_t alignment) noexcept
{
	::operator delete(pointer, alignment);
}

void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
	::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
	::operator delete(pointer, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	return ::operator new(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
	return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	return ::operator new(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	return ::operator new(size, alignment);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
	::operator delete(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
	::operator delete(pointer);
}

void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	::operator delete(pointer, alignment);
}

int main(int argc, char** argv)
{
	if (argc == 2 && std::strncmp(argv[1], "--control=", 10) == 0) {
		const char* kind = argv[1] + 10;
		void* existing = rawAllocate(16);
		armed = true;
		if (std::strcmp(kind, "new") == 0)
			(void)::operator new(16);
		if (std::strcmp(kind, "aligned_new") == 0)
			(void)::operator new(16, std::align_val_t{64});
		if (std::strcmp(kind, "malloc") == 0) {
			void* (*volatile call)(std::size_t) = &std::malloc;
			(void)call(16);
		}
		if (std::strcmp(kind, "calloc") == 0) {
			void* (*volatile call)(std::size_t, std::size_t) = &std::calloc;
			(void)call(2, 8);
		}
		if (std::strcmp(kind, "realloc") == 0) {
			void* (*volatile call)(void*, std::size_t) = &std::realloc;
			(void)call(existing, 32);
		}
		if (std::strcmp(kind, "free") == 0) {
			void (*volatile call)(void*) = &std::free;
			call(existing);
		}
		std::_Exit(87);
	}
	if (argc != 1)
		return 2;
	namespace rm = read_many_mapping;
	namespace fx = rm::fixture;
	namespace ts = telemetry;
	namespace ew = telemetry::protocol::encoded;
	armed = true;
	fx::Metadata metadata{fx::scalarDescriptorBytes};
	require(metadata.valid);
	const rm::Capability capability{1, metadata.schema.fingerprint()};
	std::array<std::byte, rm::capabilityBytes> capabilityRecord{};
	require(bool(rm::writeCapability(capabilityRecord, capability)));
	require(bool(rm::parseCapability(capabilityRecord)));
	std::array<std::byte, 2048> request{}, reply{};
	std::array<std::byte, fx::serviceModel.maxScratch() + 32> storage{};
	ts::Workspace workspace{storage};
	rm::Batch batch;
	for (std::size_t count : {std::size_t{1}, std::size_t{32}}) {
		const auto ids = fx::selection(count);
		const auto captured =
		    rm::capture(metadata.schema, capability, 1, std::span{ids}.first(count));
		require(bool(captured));
		const auto built = rm::writeRequest(request, captured.value, capability);
		require(bool(built));
		const auto result = rm::dispatch(fx::scalarModel.view(), workspace, capability,
		                                 rm::Input{request}.first(built.written), reply, batch);
		require(bool(result));
		const auto parsed =
		    rm::validateReply(rm::Input{reply}.first(result.written), captured.value, capability);
		require(bool(parsed) && parsed.value.count == count);
		for (std::size_t i = 0; i < count; ++i)
			require(rm::detail::load<std::uint32_t>(parsed.value.items[i].payload, 0) ==
			        read_comparison::data[fx::linearId(ids[i])]);
		fx::FixedRequest native{};
		native.count = static_cast<std::uint16_t>(count);
		std::copy_n(ids.begin(), count, native.ids.begin());
		require(ts::encode(native, rm::Output{request}.subspan(
		                               32, ts::wireSize<fx::FixedRequest>)) == ts::CodecStatus::Ok);
		ew::Header header;
		header.operation = ew::Operation::Service;
		header.session = 1;
		header.requestId = 1;
		header.payloadBytes = ts::wireSize<fx::FixedRequest>;
		header.expectedResponseBytes = ts::wireSize<fx::FixedReply>;
		require(ew::writeHeader(request, header) == ew::Status::Ok);
		fx::serviceOwner.fields = fx::scalarModel.view();
		fx::serviceOwner.workspace = &workspace;
		fx::serviceOwner.batch = &batch;
		const auto served = ts::integration::encoded::dispatchEncoded(
		    fx::serviceModel.view(), workspace, 1,
		    rm::Input{request}.first(32 + ts::wireSize<fx::FixedRequest>), reply);
		require(bool(served));
		const auto parsedService = ew::parse(rm::Input{reply}.first(served.written));
		require(bool(parsedService) && parsedService.header.dispatch == 0 &&
		        parsedService.header.endpointStatus == 0);
		fx::FixedReply decoded{};
		require(ts::decode(parsedService.payload, decoded) == ts::CodecStatus::Ok &&
		        decoded.count == count);
	}
	fx::serviceOwner.workspace = nullptr;
	fx::serviceOwner.batch = nullptr;
	const std::array<std::uint32_t, 2> duplicate{0, 0};
	require(rm::execute(fx::scalarModel.view(), duplicate, workspace, batch) ==
	        rm::GlobalStatus::Duplicate);
	armed = false;
	std::printf("ReadMany noheap checks=%u wrapped_c_allocations=%u\n", checks,
#if defined(READ_MANY_WRAP_ALLOCATIONS)
	            1u
#else
	            0u
#endif
	);
}
