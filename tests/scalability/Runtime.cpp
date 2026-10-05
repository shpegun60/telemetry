/*
 * @file Runtime.cpp
 * @brief Host scaling of real erased endpoint indexes over caller-owned rows.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 *
 * Isolate positional lookup from template instantiation. The fixture copies
 * real direct-owner thunks into coherent borrowed row arrays; it does not
 * claim to instantiate a typed Model with 65536 definitions. Host allocation
 * belongs to this fixture and occurs before the timed library operations.
 */
#include <telemetry/Telemetry.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <vector>

namespace ts = telemetry;

struct Request {
	std::uint32_t value;
};

struct Reply {
	std::uint32_t value;
};

// One independent application state per row, retained for all borrowed calls.
// Public methods: read(): Read state; write(): Assign state; execute(): Apply request;
// query(): Produce reply.
struct Owner {
	std::uint32_t value;

	std::uint32_t read() const noexcept
	{
		return value;
	}

	ts::WriteResult write(std::uint32_t next) noexcept
	{
		value = next;
		return ts::WriteResult::Applied;
	}

	ts::CommandResult execute(const Request& request) noexcept
	{
		value = request.value;
		return ts::CommandResult::Executed;
	}

	Reply query(const Request& request) const noexcept
	{
		return {value + request.value};
	}
};

unsigned checks = 0;

void require(bool condition)
{
	++checks;
	if (!condition) {
		std::fprintf(stderr, "Scalability condition %u failed\n", checks);
		std::abort();
	}
}

std::uint32_t load(const std::array<std::byte, 4>& bytes)
{
	std::uint32_t value = 0;
	for (unsigned i = 0; i < bytes.size(); ++i)
		value |= std::uint32_t(std::to_integer<unsigned>(bytes[i])) << (i * 8);
	return value;
}

// A single compiled function serves every population size: no N-specific path.
[[gnu::noinline]] const ts::FieldEntry* findField(const ts::FieldIndex& index,
                                                  std::uint32_t id) noexcept
{
	return index.find(id);
}

[[gnu::noinline]] std::uint32_t readField(const ts::FieldIndex& index, std::uint32_t id,
                                          ts::Workspace& workspace) noexcept
{
	std::array<std::byte, 4> bytes;
	const auto result = index.readEncoded(id, bytes, workspace);
	return result.dispatch == ts::DispatchStatus::Ok ? load(bytes) : 0;
}

[[gnu::noinline]] unsigned command(const ts::CommandIndex& index, std::uint32_t id,
                                   std::span<const std::byte> bytes,
                                   ts::Workspace& workspace) noexcept
{
	const auto result = index.executeEncoded(id, bytes, workspace);
	return static_cast<unsigned>(result.dispatch);
}

[[gnu::noinline]] unsigned writeField(const ts::FieldIndex& index, std::uint32_t id,
                                      std::span<const std::byte> bytes,
                                      ts::Workspace& workspace) noexcept
{
	const auto result = index.writeEncoded(id, bytes, workspace);
	return static_cast<unsigned>(result.dispatch);
}

[[gnu::noinline]] std::uint32_t service(const ts::ServiceIndex& index, std::uint32_t id,
                                        std::span<const std::byte> bytes,
                                        ts::Workspace& workspace) noexcept
{
	std::array<std::byte, 4> output;
	const auto result = index.callEncoded(id, bytes, output, workspace);
	return result.dispatch == ts::DispatchStatus::Ok ? load(output) : 0;
}

std::uint32_t packed(std::uint32_t row)
{
	return ((row >> 8) << 16) | (row & 255u);
}

template<class Operation>
double medianTime(Operation operation, unsigned count, bool spread)
{
	std::array<double, 5> samples;
	constexpr unsigned iterations = 200000;
	std::uint64_t checksum = 0;
	for (unsigned warm = 0; warm < 256; ++warm)
		checksum += operation(packed(warm & (count - 1)));
	for (auto& sample : samples) {
		std::uint32_t state = 17;
		const auto start = std::chrono::steady_clock::now();
		for (unsigned i = 0; i < iterations; ++i) {
			// Every profile uses the same generator, including the fixed-ID control.
			state = state * 1664525u + 1013904223u;
			const auto row = spread ? state & (count - 1) : count - 1;
			checksum += operation(packed(row));
			std::atomic_signal_fence(std::memory_order_seq_cst);
		}
		const auto elapsed = std::chrono::steady_clock::now() - start;
		sample = std::chrono::duration<double, std::nano>(elapsed).count() / iterations;
	}
	// Observable output and noninlined operations prevent a discarded benchmark.
	std::printf("\"checksum\":%llu,", static_cast<unsigned long long>(checksum));
	std::sort(samples.begin(), samples.end());
	return samples[2];
}

void profile(unsigned count)
{
	const unsigned groups = (count + 255) / 256;
	std::vector<Owner> owners(count);
	std::vector<ts::FieldEntry> fields;
	std::vector<ts::CommandEntry> commands;
	std::vector<ts::ServiceEntry> services;
	fields.reserve(count);
	commands.reserve(count);
	services.reserve(count);
	const ts::FieldTable fieldSeed{ts::field<&Owner::read, &Owner::write>("Value", owners[0])};
	const ts::CommandTable commandSeed{ts::command<&Owner::execute>("Apply", owners[0])};
	const ts::ServiceTable serviceSeed{ts::service<&Owner::query>("Query", owners[0])};
	for (unsigned i = 0; i < count; ++i) {
		owners[i].value = i + 1;
		fields.push_back(fieldSeed[0]);
		fields.back().readContext = fields.back().writeContext = &owners[i];
		commands.push_back(commandSeed[0]);
		commands.back().context = &owners[i];
		services.push_back(serviceSeed[0]);
		services.back().context = &owners[i];
	}
	std::vector<ts::FieldCatalog> fc;
	std::vector<ts::CommandCatalog> cc;
	std::vector<ts::ServiceCatalog> sc;
	for (unsigned group = 0; group < groups; ++group) {
		const auto first = group * 256;
		const auto rows = std::min(256u, count - first);
		fc.push_back({"Fields", fields.data() + first, rows});
		cc.push_back({"Commands", commands.data() + first, rows});
		sc.push_back({"Services", services.data() + first, rows});
	}
	const ts::FieldIndex fi{fc.data(), groups};
	const ts::CommandIndex ci{cc.data(), groups};
	const ts::ServiceIndex si{sc.data(), groups};
	ts::Workspace workspace{std::span<std::byte>{}};
	std::array<std::byte, 4> input;
	require(ts::encode(Request{7}, input) == ts::CodecStatus::Ok);
	for (unsigned row = 0; row < count; ++row) {
		const auto id = packed(row);
		require(fi.find(id) == &fields[row]);
		std::array<std::byte, 4> output;
		const auto read = fi.readEncoded(id, output, workspace);
		require(read.dispatch == ts::DispatchStatus::Ok && read.written == output.size() &&
		        load(output) == row + 1);
		const auto call = si.callEncoded(id, input, output, workspace);
		require(call.dispatch == ts::DispatchStatus::Ok &&
		        call.endpointStatus == ts::ServiceStatus::Ok && call.written == output.size() &&
		        load(output) == row + 8);
		const auto write = fi.writeEncoded(id, input, workspace);
		require(write.dispatch == ts::DispatchStatus::Ok &&
		        write.endpointStatus == ts::WriteResult::Applied);
		require(owners[row].value == 7);
		owners[row].value = row + 1;
		const auto execute = ci.executeEncoded(id, input, workspace);
		require(execute.dispatch == ts::DispatchStatus::Ok &&
		        execute.endpointStatus == ts::CommandResult::Executed);
		require(owners[row].value == 7);
		owners[row].value = row + 1;
	}
	require(!fi.find(packed(count)));
	require(!ci.find(packed(count)));
	require(!si.find(packed(count)));
	require(!fi.find(std::uint64_t{1} << 32));
	require(workspace.used() == 0);
	for (bool spread : {false, true}) {
		for (unsigned operation = 0; operation < 5; ++operation) {
			std::printf("{\"rows_per_family\":%u,\"catalogs\":%u,\"spread\":%s,"
			            "\"operation\":%u,",
			            count, groups, spread ? "true" : "false", operation);
			const auto time = medianTime(
			    [&](std::uint32_t id) -> std::uint64_t {
				    switch (operation) {
					    case 0:
						    return static_cast<std::uint64_t>(findField(fi, id) - fields.data());
					    case 1:
						    return readField(fi, id, workspace);
					    case 2:
						    return writeField(fi, id, input, workspace);
					    case 3:
						    return command(ci, id, input, workspace);
					    default:
						    return service(si, id, input, workspace);
				    }
			    },
			    count, spread);
			std::printf("\"median_ns_per_operation\":%.3f}\n", time);
		}
	}
}

int main()
{
	for (unsigned count : {32u, 128u, 1024u, 16384u, 65536u})
		profile(count);
	std::printf("{\"checks\":%u,\"field_entry_bytes\":%zu,\"command_entry_bytes\":%zu,"
	            "\"service_entry_bytes\":%zu,\"field_catalog_bytes\":%zu,"
	            "\"model_view_bytes\":%zu,\"timing_scope\":\"host indexes only\"}\n",
	            checks, sizeof(ts::FieldEntry), sizeof(ts::CommandEntry), sizeof(ts::ServiceEntry),
	            sizeof(ts::FieldCatalog), sizeof(ts::ModelView));
}
