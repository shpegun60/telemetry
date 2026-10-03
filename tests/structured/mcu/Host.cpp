/* Offline execution of probe bodies selected for later MCU measurements.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT. */
#include "Fixture.hpp"
#ifndef QUALIFICATION_ARM
#include <cstdio>
#else
// A freestanding fixture has no process/signal implementation. Keep failed
// contracts deterministic and allocation-free; do not pull newlib's hosted
// abort -> raise path into the linked ARM comparison image.
extern "C" [[noreturn]] void abort() noexcept { __builtin_trap(); }
#endif

int main()
{
    telemetry::requireStructuredAbi();
#ifndef MCU_SCALE
    qualification::consumer_typed();
    qualification::consumer_encoded();
#endif
    [[maybe_unused]] const auto consumerChecks = qualification::checks;
    mcu::checkProbes();
#ifndef QUALIFICATION_ARM
    std::printf("{\"checks\":%u,\"failures\":%u,\"consumer_checks\":%u}\n",
                qualification::checks, qualification::failures, consumerChecks);
#endif
    return qualification::failures == 0 ? 0 : 1;
}
