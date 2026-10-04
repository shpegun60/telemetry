/* Each retained compiled boundary must reject a differently built provider.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"

int main()
{
	const auto view = qualification::modelView();
	telemetry::Workspace workspace{std::span<std::byte>{}};
#if CASE == 1
	const auto result = telemetry::readFieldEncoded(view, 0u, {}, workspace).dispatch;
	const auto expected = telemetry::DispatchStatus::BufferTooSmall;
#elif CASE == 2
	const auto result = telemetry::writeFieldEncoded(view, 0u, {}, workspace).dispatch;
	const auto expected = telemetry::DispatchStatus::InvalidPayload;
#elif CASE == 3
	const auto result = telemetry::executeCommandEncoded(view, 0u, {}, workspace).dispatch;
	const auto expected = telemetry::DispatchStatus::InvalidPayload;
#elif CASE == 4
	const auto result = telemetry::callServiceEncoded(view, 0u, {}, {}, workspace).dispatch;
	const auto expected = telemetry::DispatchStatus::InvalidPayload;
#else
#error Select one retained ABI boundary
#endif
	qualification::check(result == expected);
#ifndef QUALIFICATION_ARM
	std::printf("{\"checks\":%u,\"failures\":%u}\n", qualification::checks,
	            qualification::failures);
#endif
	return qualification::failures == 0 ? 0 : 1;
}
