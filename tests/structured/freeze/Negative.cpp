/* Invalid public declarations must stay rejected at the factory boundary.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include <telemetry_structured/Structured.hpp>
namespace ts = telemetry::structured;
struct Request { std::uint32_t value; };
#if CASE == 1
auto invalid = ts::field("Value", "V", +[]() noexcept { return 1.0f; });
#elif CASE == 2
int wrongResult() noexcept { return 0; }
auto invalid = ts::command<&wrongResult>("Wrong result");
#elif CASE == 3
telemetry::CommandResult scalarRequest(std::uint32_t) noexcept { return telemetry::CommandResult::Executed; }
auto invalid = ts::command<&scalarRequest>("Scalar request");
#elif CASE == 4
std::uint32_t scalarResponse() noexcept { return 0; }
auto invalid = ts::service<&scalarResponse>("Scalar response");
#elif CASE == 5
float throwingGetter() { return 1.0f; }
auto invalid = ts::field<&throwingGetter>("Throwing target");
#elif CASE == 6
telemetry::CommandResult pointerRequest(const Request*) noexcept { return telemetry::CommandResult::Executed; }
auto invalid = ts::command<&pointerRequest>("Pointer request");
#elif CASE == 7
auto invalid = ts::command("Metadata", +[]() noexcept { return telemetry::CommandResult::Executed; }, 1);
#elif CASE == 8
struct OtherRequest { std::uint32_t value; };
Request readRequest() noexcept { return {}; }
void invalid() { (void)ts::field<&readRequest>("Value").readAs<OtherRequest>(); }
#elif CASE == 9
using Invalid = ts::Scalar;
#elif CASE == 10
Request response() noexcept { return {}; }
auto invalid = ts::service("Metadata", &response, "unit");
#endif
