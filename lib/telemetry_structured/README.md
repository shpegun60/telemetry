# Structured telemetry (under construction)

Authors: Ruslan Kovtun (shpegun60), codexAi. Own code: MIT.

The new C++20 module follows the
[structured v3 implementation plan](../../doc/StructuredTelemetryV3ImplementationPlan.md).
Its current implementation is the stable
[reflection facade](reflection/Reflection.hpp): aggregate member facts and
access, callable signature facts, and the enum customization point. The
normalized enum dictionary, codec, model, service bindings and wire format
belong to later stages and are not exposed as working APIs yet.

Only the backend adapter includes the separately licensed
[Boost.PFR source](../boost_pfr/VERSION.md). The existing scalar telemetry
library does not depend on this module. Focused verification and the exact
compiler matrix are in [tests/structured](../../tests/structured/README.md).
