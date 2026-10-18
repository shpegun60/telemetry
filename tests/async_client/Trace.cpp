/* Execute the shared web/C++ action traces. Authors: Ruslan Kovtun (shpegun60), codexAi. MIT. */
#include "Fixture.hpp"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace af = async_fixture;
namespace cl = telemetry::client;

struct TraceBackend : af::Backend {
	bool prepared = false;

	cl::Status start(cl::RequestView view, std::uint32_t now) noexcept override
	{
		const auto status = af::Backend::start(view, now);
		if (status == cl::Status::Ok) {
			prepared = false;
			std::printf("START %u %u ", unsigned(view.operation), view.endpointId);
			if (view.payload.empty())
				std::printf("-");
			for (const auto byte : view.payload)
				std::printf("%02x", std::to_integer<unsigned>(byte));
			std::printf("\n");
		}
		return status;
	}

	void cancel(cl::Status reason) noexcept override
	{
		if (!active)
			return;
		if (prepared) {
			recovery = true;
			opened = false;
			if (request.operation != cl::Operation::FieldRead)
				reason = cl::Status::OutcomeUnknown;
		}
		af::Backend::cancel(reason);
	}
};

int main()
{
	af::now = 0;
	TraceBackend backend;
	cl::DeviceClient<af::Codec, 3, 12> client{backend, af::clock};
	std::unordered_map<std::string, cl::RequestHandle> handles;
	std::string line;
	while (std::getline(std::cin, line)) {
		std::istringstream input{line};
		std::string action;
		input >> action;
		if (action == "OPEN") {
			unsigned generation;
			input >> generation;
			(void)client.admitDescriptor(af::Codec::header, generation);
		} else if (action == "SUB") {
			std::string tag;
			unsigned op, value, timeout;
			input >> tag >> op >> value >> timeout;
			std::array<std::byte, 4> bytes{};
			for (unsigned i = 0; i < 4; ++i)
				bytes[i] = std::byte((value >> (8 * i)) & 255);
			const cl::Input payload = op == 4 ? cl::Input{} : cl::Input{bytes};
			auto submitted = client.submitEncoded(
			    {static_cast<cl::Operation>(op), 7, payload, (op == 3 || op == 4) ? 4U : 0U},
			    [tag](const cl::ResultView& result) noexcept {
				    std::printf("DONE %s %u %u %u ", tag.c_str(), unsigned(result.status),
				                result.dispatch, result.endpointStatus);
				    if (result.succeeded() && result.payload.size() == 4) {
					    unsigned value;
					    std::memcpy(&value, result.payload.data(), 4);
					    std::printf("%u\n", value);
				    } else
					    std::printf("-\n");
			    },
			    timeout);
			if (submitted)
				handles[tag] = submitted.handle;
			else
				std::printf("DONE %s %u 0 0 -\n", tag.c_str(), unsigned(submitted.status));
		} else if (action == "HANDOFF") {
			if (!backend.active)
				return 3;
			backend.prepared = true;
		} else if (action == "REPLY") {
			unsigned dispatch, endpoint;
			std::string value;
			input >> dispatch >> endpoint >> value;
			if (!backend.active)
				return 4;
			backend.finish(value == "-" ? 0 : static_cast<unsigned>(std::stoul(value)));
			backend.outcome.dispatch = static_cast<std::uint8_t>(dispatch);
			backend.outcome.endpointStatus = static_cast<std::uint8_t>(endpoint);
			if (value == "-")
				backend.outcome.payload = {};
		} else if (action == "ADVANCE") {
			unsigned elapsed;
			input >> elapsed;
			af::now += elapsed;
		} else if (action == "CANCEL") {
			std::string tag;
			input >> tag;
			(void)client.cancel(handles[tag]);
		} else if (action == "CONFIRM") {
			unsigned generation;
			input >> generation;
			(void)client.confirmSessionClosed(generation);
		} else if (action == "RESET") {
			backend.opened = false;
			backend.cancel(cl::Status::SessionLost);
		} else if (action != "PUMP")
			return 2;
		for (unsigned step = 0; step < 5; ++step)
			client.process(af::now);
	}
	if (client.pending())
		return 5;
	client.close();
	client.process(af::now);
	return client.drained() ? 0 : 6;
}
