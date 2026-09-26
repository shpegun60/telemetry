// Public API regressions from the September 2026 technical review.
// Authors: Ruslan Kovtun (shpegun60), codexAi. License: MIT.
#include "Telemetry.h"
#include "serialization/TelemetryJson.h"
#include "serialization/TelemetryCommandJson.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

using namespace telemetry;
namespace {
unsigned checks = 0, writes = 0, calls = 0;
float stored = 1.f;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "line %d: %s\n", __LINE__, #__VA_ARGS__); std::abort(); } } while (false)

float reviewRead() noexcept { return stored; }
WriteResult reviewWrite(float value) noexcept { ++writes; stored = value; return WriteResult::Applied; }
CommandResult run(std::uint32_t) noexcept { ++calls; return CommandResult::Executed; }
constexpr FieldTable rows{field<&reviewRead, &reviewWrite>("value", "")};
constexpr FieldCatalogTable groups{group("g", rows)};
constexpr CommandTable commands{command<&run>("run")};
constexpr CommandTable emptyMetadata{command<&run>("run", commandArgs())};
constexpr auto makeCommands() noexcept
{
    return CommandTable{command<&run>("run", arg<0>("value", "", std::uint32_t{1}))};
}
constexpr auto factoryCommands = makeCommands();
constexpr CommandCatalogTable commandGroups{group("g", commands)};
constexpr Catalog catalogs[]{Catalog{"g", rows.data(), rows.size()}};
constexpr auto staticIndex = CatalogIndex::bind<catalogs>();

unsigned long storedLong = 0;
WriteResult writeLong(unsigned long value) noexcept { storedLong = value; return WriteResult::Applied; }

struct Owner {
    float value = 4.f;
    float read() const noexcept { return value; }
    WriteResult write(float next) noexcept { value = next; return WriteResult::Applied; }
};
struct Derived : Owner {};
struct ReferenceTemplate {
    template <class T> void operator()(T& value) const noexcept { value = 19; }
    void operator()(int) const noexcept {}
};
}

int main()
{
    static_assert(makeId(65535, 65535) == UINT32_MAX);
    static_assert(!tryMakeId(0, 65536) && !tryMakeId(-1, 0));
    static_assert(tryMakeId(1, 2) == makeId(GroupId{1}, EntryOffset{2}));
    static_assert(!std::is_constructible_v<decltype(field<&reviewRead>("x", "")), Field>);
    static_assert(!CommandArgs<>::positional && CommandArgs<>::indexed);

    const auto index = groups.index();
    for (std::uint64_t bad : {UINT64_C(0x100000000), UINT64_C(0x100000001), UINT64_MAX}) {
        CHECK(index.find(bad) == nullptr && !index.read<float>(bad));
        CHECK(index.read(bad).type() == ScalarType::Null);
        CHECK(index.write(bad, 5) == WriteResult::NotFound);
        CHECK(!groups.read<float>(bad) && groups.write(bad, 5) == WriteResult::NotFound);
        CHECK(staticIndex.find(bad) == nullptr && !staticIndex.read<float>(bad));
        CHECK(staticIndex.write(bad, 5) == WriteResult::NotFound);
        CHECK(commands.index().execute(bad, nullptr, 0) == CommandResult::NotFound);
        CHECK(commands.call(bad, 5) == CommandResult::NotFound);
        CHECK(commandGroups.index().call(bad, 5) == CommandResult::NotFound);
        CHECK(commandGroups.execute(bad, nullptr, 0) == CommandResult::NotFound);
    }
    CHECK(index.find(-1) == nullptr && index.catalog(65536) == nullptr);
    CHECK(commandGroups.index().find(-1) == nullptr && commandGroups.index().catalog(65536) == nullptr);
    CHECK(commands.call(-1, 5) == CommandResult::NotFound);
    CHECK(writes == 0 && calls == 0);
    CHECK(index.write(0, 12) == WriteResult::Applied && stored == 12.f && writes == 1);
    CHECK(commands.call<0>(1) == CommandResult::Executed && calls == 1);
    CHECK(emptyMetadata.call<0>(1) == CommandResult::Executed && calls == 2);
    CHECK(emptyMetadata.data()[0].metadata == nullptr && emptyMetadata.data()[0].parameterCount() == 1);
    CHECK(factoryCommands.call<0>(2) == CommandResult::Executed && calls == 3);

    // Manual rows normalize to their declared type before adapting a native
    // setter. Failure must not call that setter or inspect a getter.
    const Field f64{"f64", "", ScalarType::F64, nullptr, &reviewWrite};
    CHECK(f64.write(250) == WriteResult::Applied && stored == 250.f);
    const auto before = writes;
    CHECK(f64.write(1.e300) == WriteResult::InvalidValue && writes == before);
    const Field u16{"u16", "", ScalarType::U16, nullptr, &reviewWrite};
    CHECK(u16.write(12.7) == WriteResult::Applied && stored == 12.f);
    CHECK(u16.write(-1) == WriteResult::InvalidValue);
    const Field u32{"u32", "", ScalarType::U32, nullptr, &writeLong};
    CHECK(u32.write(UINT32_MAX) == WriteResult::Applied && storedLong == UINT32_MAX);

    // Direct object, explicit dereference and base-class adjustment all remain
    // valid after pointer-like owner variables and converting proxies are rejected.
    Derived derived;
    Owner* pointer = &derived;
    const FieldTable bound{field<&Owner::read, &Owner::write>("derived", "", derived),
                           field<&Owner::read>("deref", "", *pointer)};
    CHECK(bound.read<0>() == 4.f && bound.read<1>() == 4.f);
    CHECK(bound.write<0>(7) == WriteResult::Applied && derived.value == 7.f);
    OwnerSlot<Owner> slot;
    const FieldTable delayed{field<&Owner::read>("slot", "", slot)};
    CHECK(!delayed.read<0>());
    slot.bind(derived);
    CHECK(delayed.read<0>() == 7.f);

    auto genericRef = [](auto& value) noexcept { value = 5; };
    auto forwardingRef = [](auto&& value) noexcept { value = 7; };
    auto genericValue = [](auto value) noexcept { return value + 1; };
    auto wrong = [](auto value) noexcept { (void)value; };
    static_assert(!detail::slotSignatureMatches<decltype(wrong), void, int&>);
    DelegateSlot<void(int&) noexcept> owned;
    DelegateRefSlot<void(int&) noexcept> borrowed;
    int value = 0;
    owned.bind(genericRef);
    owned.invoke(value);
    CHECK(value == 5);
    borrowed.bind(forwardingRef);
    borrowed.invoke(value);
    CHECK(value == 7);
    ReferenceTemplate mixed;
    borrowed.bind(mixed);
    borrowed.invoke(value);
    CHECK(value == 19);
    DelegateSlot<int(int) noexcept> native;
    native.bind(genericValue);
    CHECK(native.invoke(4) == 5);

    // Zero is a real FNV-1a result, independent of invalid metadata status.
    const Catalog zero[]{{"!8pV5U", nullptr, 0}};
    char json[1024];
    const auto fingerprint = trySchemaCrc(zero, 1);
    CHECK(fingerprint && *fingerprint == 0 && schemaCrc(zero, 1) == 0);
    CHECK(writeSchema(zero, 1, json, sizeof json) != 0 &&
          std::strstr(json, "\"schema\":\"00000000\"") != nullptr);
    const Catalog invalid[]{{nullptr, nullptr, 0}};
    CHECK(!trySchemaCrc(invalid, 1));
    CHECK(writeSchema(invalid, 1, json, sizeof json) == 0);
    CHECK(trySchemaCrc(commands.index()).has_value());
    CHECK(trySchemaCrc(commandGroups.index()).has_value());
    std::printf("Review public API: %u checks passed\n", checks);
}
