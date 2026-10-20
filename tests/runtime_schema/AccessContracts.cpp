/*
 * @file AccessContracts.cpp
 * @brief Guard-page proof of metadata-only navigation and bounded prepared reads.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include <telemetry/runtime/DynamicValueView.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace rt = telemetry::runtime;
static unsigned checks = 0;

void check(bool pass, int line)
{
	++checks;
	if (!pass) {
		std::fprintf(stderr, "access check %u failed at %d\n", checks, line);
		std::exit(1);
	}
}

#define CHECK(...) check(bool(__VA_ARGS__), __LINE__)
static volatile rt::Status sink = rt::Status::Stale;

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

int main(int argc, char** argv)
{
	if (argc != 2)
		return 2;
	std::array<std::byte, 4096> descriptor{};
	auto* f = std::fopen(argv[1], "rb");
	CHECK(f != nullptr);
	const auto n = std::fread(descriptor.data(), 1, descriptor.size(), f);
	CHECK(n > 0 && n < descriptor.size());
	CHECK(std::fclose(f) == 0);
	std::array<rt::RuntimeType, 16> types{};
	std::array<rt::RuntimeMember, 4> members{};
	std::array<rt::RuntimeEnumEntry, 0> enums{};
	std::array<rt::RuntimeCatalog, 0> catalogs{};
	std::array<rt::RuntimeEndpoint, 0> endpoints{};
	rt::RuntimeSchemaWorkspace workspace{types, members, enums, catalogs, endpoints};
	const auto parsed = rt::RuntimeSchema::parse({descriptor.data(), n}, workspace);
	CHECK(parsed);
	const auto root = telemetry::TypeId{13};
	CHECK(parsed.schema.type(root)->wireBytes == 65537);
	const auto page = std::size_t(sysconf(_SC_PAGESIZE));
	CHECK(page > 0 && page < 65537);
	const auto allocation = (65537 + page - 1) / page * page;
	auto* data = static_cast<std::byte*>(
	    mmap(nullptr, allocation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
	CHECK(data != MAP_FAILED);
	std::memset(data, 0, allocation);
	const auto created = parsed.schema.view(root, {data, 65537});
	CHECK(created);
	auto view = created.value;
	const auto first = parsed.schema.resolve(root, "first");
	CHECK(first);
	const auto last = parsed.schema.resolve(root, "body[65535]");
	CHECK(last);
	CHECK(last.value.wireOffset() == 65536);
	CHECK(mprotect(data, allocation, PROT_NONE) == 0);
	// No payload byte is readable: both indexed navigations must be metadata-only.
	const auto firstView = view.member(std::uint32_t{0});
	CHECK(firstView);
	CHECK(firstView.value.typeId() == 1);
	CHECK(firstView.value.wireOffset() == 0);
	const auto array = view.member(std::uint32_t{1});
	CHECK(array);
	const auto lastView = array.value.index(65535);
	CHECK(lastView);
	CHECK(lastView.value.wireOffset() == 65536);
	CHECK(view.member("body").value.index(0));
	CHECK(view.bytes().value.size() == 65537);
	auto schemaCopy = parsed.schema;
	auto schemaMove = std::move(schemaCopy);
	CHECK(schemaMove.valid());
	rt::RuntimeSchema schemaAssigned;
	schemaAssigned = parsed.schema;
	rt::RuntimeSchema schemaMoveAssigned;
	schemaMoveAssigned = std::move(schemaAssigned);
	CHECK(schemaMoveAssigned.valid());
	auto copied = view;
	auto moved = std::move(copied);
	CHECK(moved.valid());
	rt::DynamicValueView assigned;
	assigned = view;
	rt::DynamicValueView moveAssigned;
	moveAssigned = std::move(assigned);
	CHECK(moveAssigned.valid());
	CHECK(moved.member(1u).value.index(65535));
	auto handleCopy = first.value;
	auto handleMoved = std::move(handleCopy);
	CHECK(schemaMove.accepts(handleMoved));
	rt::PreparedHandle handleAssigned;
	handleAssigned = first.value;
	rt::PreparedHandle handleMoveAssigned;
	handleMoveAssigned = std::move(handleAssigned);
	CHECK(schemaMoveAssigned.accepts(handleMoveAssigned));
	// Restore only the first page. Prepared scalar reads can touch their selected
	// leaf, but a hidden revalidation of the root or array faults on its tail.
	CHECK(mprotect(data, page, PROT_READ | PROT_WRITE) == 0);
	const auto readFalse = [](auto result) {
		CHECK(result);
		CHECK(!result.value);
	};
	readFalse(view.get<bool>(first.value));
	readFalse(moved.get<bool>(handleMoved));
	readFalse(moveAssigned.get<bool>(handleMoveAssigned));
	readFalse(firstView.value.get<bool>());
	readFalse(array.value.index(0).value.get<bool>());
	CHECK(view.get<std::uint8_t>(first.value).status == rt::Status::TypeMismatch);
	CHECK(view.get<bool>(rt::PreparedHandle{}).status == rt::Status::Stale);
	// Both controls must fault. The second proves a complete validate really
	// visits protected bool bytes; it is independent of access timing.
	faultControl([&] {
		auto* p = reinterpret_cast<const volatile unsigned char*>(data + page);
		volatile auto value = *p;
		(void)value;
	});
	faultControl([&] {
		sink = parsed.schema.view(root, {data, 65537}).status;
	});
	CHECK(mprotect(data, allocation, PROT_NONE) == 0);
	CHECK(workspace.reset() == rt::Status::Ok);
	CHECK(view.get<bool>(first.value).status == rt::Status::Stale);
	CHECK(moved.member(0u).status == rt::Status::Stale);
	CHECK(moveAssigned.bytes().status == rt::Status::Stale);
	CHECK(!schemaMove.valid());
	CHECK(!schemaMoveAssigned.accepts(handleMoveAssigned));
	CHECK(munmap(data, allocation) == 0);
	std::printf("access contracts: %u checks, 0 failures; two SIGSEGV negative controls\n", checks);
}
