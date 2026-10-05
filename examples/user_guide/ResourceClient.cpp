// Resource client packets through an in-process complete-packet transport.
// This demonstrates the client API, not UART/TCP framing or a network session.
// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
#include <resource/BytesFile.hpp>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Client.hpp>
#include <resource/protocol/Protocol.hpp>
#include <array>
#include <cstdio>

namespace client = resource::protocol::client;

int main()
{
	const std::array<std::byte, 3> deviceBytes{std::byte{10}, std::byte{20}, std::byte{30}};
	resource::BytesFile provider(deviceBytes);
	const auto files = resource::filesystem(resource::file("/device", provider));
	std::array<std::byte, 32> request{};
	std::array<std::byte, 32> response{};

	// A real transport sends only this prefix, then collects one complete reply.
	// The returned Input borrows response, which the next exchange reuses.
	const auto exchange = [&](client::BuildResult built) -> resource::Input {
		if (built.status != client::BuildStatus::Ok) {
			return {};
		}
		const auto reply = resource::protocol::process(
		    files.view(), resource::Input(request).first(built.written), response);
		return resource::Input(response).first(reply.written);
	};

	{
		const auto listed = client::parseList(exchange(client::makeList(request, 0)));
		if (!listed || listed.shortError || listed.response.status != resource::Status::Ok) {
			return 1;
		}
		for (const auto path : listed.response.paths) {
			std::printf("File: %.*s\n", static_cast<int>(path.size()), path.data());
		}
		// All borrowed paths have been used before the next exchange.
	}

	const auto stat = client::parseStat(exchange(client::makeStat(request, 0)));
	if (!stat || stat.shortError || stat.response.status != resource::Status::Ok ||
	    stat.response.size != deviceBytes.size()) {
		return 2;
	}

	resource::Cursor cursor = 0;
	for (;;) {
		const auto read = client::parseRead(exchange(client::makeRead(request, 0, cursor)));
		if (!read || read.shortError || read.response.status != resource::Status::Ok) {
			return 3;
		}
		for (const auto byte : read.response.data) {
			std::printf("Byte: %u\n", std::to_integer<unsigned>(byte));
		}
		if (read.response.eof) {
			break;
		}
		if (read.response.data.empty() && read.response.next == cursor) {
			return 4;
		}
		cursor = read.response.next;
	}

	// BytesFile is read-only. A valid remote error still parses successfully.
	const std::array<std::byte, 1> data{std::byte{40}};
	const auto written =
	    client::parseWrite(exchange(client::makeWrite(request, 0, 0, data, true)), data.size());
	if (!written || written.shortError ||
	    written.response.status != resource::Status::NotWritable) {
		return 5;
	}
	std::puts("Resource client example passed");
}
