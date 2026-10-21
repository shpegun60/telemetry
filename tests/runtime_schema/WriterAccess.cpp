/*
 * @file WriterAccess.cpp
 * @brief Protected-page proof of direct prepared writes and staged commit ordering.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueWriter.hpp>

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
namespace rt = telemetry::runtime;
unsigned checks = 0;
volatile rt::Status sink = rt::Status::Stale;

void check(bool ok, int line)
{
	++checks;
	if (!ok) {
		std::fprintf(stderr, "writer access check %u line %d failed\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)

template<class Function>
void faultControl(Function function)
{
	const auto child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		const rlimit noCore{0, 0};
		if (setrlimit(RLIMIT_CORE, &noCore) != 0)
			std::_Exit(98);
		function();
		std::_Exit(97);
	}
	int status = 0;
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFSIGNALED(status));
	CHECK(WTERMSIG(status) == SIGSEGV);
}
} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	std::array<std::byte, 4096> descriptor{};
	auto* file = std::fopen(argv[1], "rb");
	CHECK(file);
	const auto size = std::fread(descriptor.data(), 1, descriptor.size(), file);
	CHECK(size > 0 && size < descriptor.size());
	CHECK(std::fclose(file) == 0);
	std::array<rt::RuntimeType, 16> types{};
	std::array<rt::RuntimeMember, 4> members{};
	std::array<rt::RuntimeEnumEntry, 0> enums{};
	std::array<rt::RuntimeCatalog, 0> catalogs{};
	std::array<rt::RuntimeEndpoint, 0> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	const auto parsed = rt::RuntimeSchema::parse({descriptor.data(), size}, workspace);
	CHECK(parsed);
	constexpr auto root = telemetry::TypeId{13};
	constexpr std::size_t valueBytes = 65537;
	CHECK(parsed.schema.type(root)->wireBytes == valueBytes);
	const auto first = parsed.schema.resolve(root, "first");
	CHECK(first);
	const auto body = parsed.schema.resolve(root, "body[0]");
	CHECK(body);
	CHECK(first.value.wireOffset() == 0 && body.value.wireOffset() == 1);
	const auto page = std::size_t(sysconf(_SC_PAGESIZE));
	CHECK(page > 1 && page < valueBytes);
	const auto allocation = (valueBytes + page - 1) / page * page;
	auto allocate = [&](int kind) {
		auto* p = static_cast<std::byte*>(
		    mmap(nullptr, allocation, PROT_READ | PROT_WRITE, kind | MAP_ANONYMOUS, -1, 0));
		CHECK(p != MAP_FAILED);
		return p;
	};
	auto* original = allocate(MAP_PRIVATE);
	auto* staging = allocate(MAP_PRIVATE);
	// The child commit control shares output so early destination writes would
	// remain observable in the parent even if that child later faults.
	auto* output = allocate(MAP_SHARED);
	std::memset(original, 0, allocation);
	std::memset(staging, 0x7b, allocation);
	std::memset(output, 0xa5, allocation);
	auto writer = rt::DynamicValueWriter::begin(parsed.schema, root, {original, valueBytes},
	                                            {staging, valueBytes});
	CHECK(writer);
	CHECK(writer.status() == rt::Status::Ok);
	CHECK(mprotect(original, allocation, PROT_NONE) == 0);
	CHECK(mprotect(staging + page, allocation - page, PROT_NONE) == 0);
	const bool yes = true;
	CHECK(writer.set(first.value, yes) == rt::Status::Ok);
	CHECK(writer.set(body.value, yes) == rt::Status::Ok);
	CHECK(staging[0] == std::byte{1} && staging[1] == std::byte{1});
	CHECK(writer.status() == rt::Status::Ok);
	// Direct and whole-value controls prove the tail is genuinely inaccessible.
	faultControl([&] {
		auto* p = reinterpret_cast<const volatile unsigned char*>(staging + page);
		volatile auto byte = *p;
		(void)byte;
	});
	faultControl([&] {
		sink = parsed.schema.view(root, {staging, valueBytes}).status;
	});
	faultControl([&] {
		sink = writer.commit({output, valueBytes}).status;
	});
	CHECK(std::ranges::all_of(std::span{output, valueBytes}, [](auto b) {
		return b == std::byte{0xa5};
	}));
	CHECK(writer.status() == rt::Status::Ok);
	CHECK(mprotect(staging, allocation, PROT_READ) == 0);
	const auto committed = writer.commit({output, valueBytes});
	CHECK(committed);
	CHECK(committed.value == valueBytes);
	CHECK(output[0] == std::byte{1} && output[1] == std::byte{1});
	CHECK(std::ranges::all_of(std::span{output + 2, valueBytes - 2}, [](auto b) {
		return b == std::byte{0};
	}));
	CHECK(writer.status() == rt::Status::Stale);
	CHECK(mprotect(staging, allocation, PROT_NONE) == 0);
	CHECK(writer.set(first.value, yes) == rt::Status::Stale);
	CHECK(writer.commit({output, valueBytes}).status == rt::Status::Stale);
	CHECK(munmap(original, allocation) == 0);
	CHECK(munmap(staging, allocation) == 0);
	CHECK(munmap(output, allocation) == 0);
	std::printf("writer access: %u checks, 0 failures; three SIGSEGV negative controls\n", checks);
}
