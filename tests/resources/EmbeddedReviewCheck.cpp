// The MCU fixture also runs on every host resource CI configuration (MIT).
// Authors: Ruslan Kovtun (shpegun60), codexAi.
#include "../regression/EmbeddedReviewCheck.hpp"
#include <cstdio>
int main()
{
    const auto result = review_embedded::run();
    std::printf("Shared host/MCU review: %u checks, %u failures, first line %u\n",
                result.checks, result.failures, result.firstLine);
    return result.failures == 0 ? 0 : 1;
}
