// Each selected case must fail at the API boundary, never during invocation.
#include "Telemetry.h"
#include <utility>
using namespace telemetry;
float readValue() noexcept { return 1.f; }
WriteResult writeValue(float) noexcept { return WriteResult::Applied; }
struct Visitor { template <class T> void operator()(const T&) const noexcept {} };
struct ThrowingBool { operator bool() const noexcept(false) { return true; } };

#if TELEMETRY_TRAVERSAL_FAIL_CASE == 1
constexpr auto invalid = field<&readValue>("readonly", "").withFlags(FieldFlag::Persistent);
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 2
constexpr Field invalid{"writeonly", "", ScalarType::F32, nullptr, &writeValue, FieldFlag::Persistent};
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 3
constexpr auto invalid = reservedField().withFlags(FieldFlag::Persistent);
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 4
auto invalid = field<&readValue, &writeValue>("value", "").withFlags(1u);
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 5
FieldFlags invalid = std::uint32_t{1};
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 6
void fail() { Scalar{}.visit(Visitor{}); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 7
void fail() { const Scalar value; std::move(value).visit(Visitor{}); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 8
auto invalid = FieldTable<>{}.begin();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 9
auto invalid = FieldTable<>{}.end();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 10
auto invalid = CommandTable<>{}.begin();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 11
auto invalid = CommandTable<>{}.end();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 12
auto invalid = FieldCatalogTable<>{}.catalogs();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 13
auto invalid = CommandCatalogTable<>{}.catalogs();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 14
auto invalid = FieldCatalogTable<>{}.begin();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 15
auto invalid = FieldCatalogTable<>{}.end();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 16
auto invalid = CommandCatalogTable<>{}.begin();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 17
auto invalid = CommandCatalogTable<>{}.end();
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 18
void fail(const Command& command) { command.forEachParameter([](const CommandParam&) { return true; }); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 19
void fail(const Command& command) { command.forEachParameter([](const CommandParam&) noexcept {}); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 20
void fail(const Command& command) { command.forEachParameter([](int) noexcept { return true; }); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 21
void fail(const Scalar& value) { value.visit([](float) {}); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 22
void fail(const Scalar& value) { value.visit([](auto& native) { native = {}; }); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 23
void fail(Field& value) { value.flags_ = FieldFlag::Persistent; }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 24
void fail(const Command& command) { command.forEachParameter([](const CommandParam&) noexcept { return ThrowingBool{}; }); }
#elif TELEMETRY_TRAVERSAL_FAIL_CASE == 25
auto invalid = FieldRange::fromCapped(nullptr, SIZE_MAX, 0);
#else
#error Select a TELEMETRY_TRAVERSAL_FAIL_CASE
#endif
