// Review probe (catalog-json): print the ABI signature and the layout words a
// consumer computes, so builds under different standards/compilers/macros can
// be compared. Not part of the library or the runners.
#include "Telemetry.h"
#include <cstdio>

int main()
{
    using namespace telemetry;
    std::printf("cplusplus=%ld abi=%u signature=%016llx\n", static_cast<long>(__cplusplus),
                static_cast<unsigned>(telemetryAbiVersion),
                static_cast<unsigned long long>(telemetryAbiSignature));
    std::printf("cacheLine=%u Field=%u/%u Scalar=%u/%u FieldType=%u Catalog=%u Command=%u\n",
                static_cast<unsigned>(cacheLineBytes), static_cast<unsigned>(sizeof(Field)),
                static_cast<unsigned>(alignof(Field)), static_cast<unsigned>(sizeof(Scalar)),
                static_cast<unsigned>(alignof(Scalar)), static_cast<unsigned>(sizeof(FieldType)),
                static_cast<unsigned>(sizeof(Catalog)), static_cast<unsigned>(sizeof(Command)));
    telemetry::requireTelemetryAbi();
    return 0;
}
