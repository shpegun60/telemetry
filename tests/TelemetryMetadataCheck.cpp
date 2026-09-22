// Indexed metadata preserves visitor order, exact values and borrowed labels.
#include "Telemetry.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

enum class Wide : std::uint64_t { Small = 7, Large = UINT64_MAX };
enum class Signed : std::int64_t { Low = INT64_MIN, High = INT64_MAX };
namespace magic_enum::customize {
template <> constexpr customize_t enum_name<Wide>(Wide value) noexcept
{
    if (value == Wide::Small) return std::string_view{"a\0b", 3};
    return default_tag;
}
}

namespace {
using namespace telemetry;
int checks = 0, failures = 0;
void expect(bool good, const char* why)
{
    ++checks;
    if (!good) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); }
}
constexpr auto wide = enumType<Wide, Wide::Large, Wide::Small>();
constexpr auto signedType = enumType<Signed, Signed::High, Signed::Low>();
static_assert(wide.enumCount() == 2 && !FieldType{}.hasEnum() && FieldType{}.enumCount() == 0);
static_assert(wide.withDefault(std::uint64_t{8}).enumCount() == 2);
#if defined(__arm__)
static_assert(sizeof(FieldType) == 48 && sizeof(Field) == 96 && sizeof(Command) == 20);
#endif

struct Entries {
    std::vector<Scalar> values;
    std::vector<std::string_view> names;
    static bool sink(void* context, const Scalar& value, std::string_view name) noexcept
    {
        auto& self = *static_cast<Entries*>(context);
        self.values.push_back(value);
        self.names.push_back(name);
        return true;
    }
};

void enums()
{
    Entries all, indexed;
    expect(wide.describeEnum(&all, Entries::sink), "whole enum description succeeds");
    for (std::uint32_t i = 0; i < wide.enumCount(); ++i)
        expect(wide.describeEnumEntry(i, &indexed, Entries::sink), "indexed entry succeeds");
    expect(all.values.size() == 2 && indexed.values.size() == 2 && all.names == indexed.names,
           "indexed order matches explicit full traversal");
    expect(indexed.values[0].get<std::uint64_t>() == UINT64_MAX &&
           indexed.values[1].get<std::uint64_t>() == 7 && indexed.names[1].size() == 3 &&
           indexed.names[1][1] == '\0', "large codes and embedded NUL names survive");
    expect(!wide.describeEnumEntry(2, &indexed, Entries::sink) &&
           !wide.describeEnumEntry(UINT32_MAX, &indexed, Entries::sink) &&
           !wide.describeEnumEntry(0, &indexed, nullptr) && indexed.values.size() == 2,
           "bad indices and null sink cause no visit");
    expect(!FieldType{}.describeEnum(&all, Entries::sink) &&
           !FieldType{}.describeEnumEntry(0, &all, Entries::sink) &&
           !wide.describeEnum(nullptr, nullptr), "missing enum and null sink return false");
    unsigned calls = 0;
    const auto stop = +[](void* context, const Scalar&, std::string_view) noexcept {
        ++*static_cast<unsigned*>(context); return false;
    };
    expect(!wide.describeEnum(&calls, stop) && calls == 1, "full traversal stops immediately");
    expect(!wide.describeEnumEntry(1, &calls, stop) && calls == 2, "indexed result reflects sink refusal");
    Entries signedEntries;
    expect(signedType.describeEnum(&signedEntries, Entries::sink) &&
           signedEntries.values[0].get<std::int64_t>() == INT64_MAX &&
           signedEntries.values[1].get<std::int64_t>() == INT64_MIN,
           "signed 64-bit extrema and explicit order survive");
    enum class Automatic { A, B, C };
    constexpr auto automatic = enumType<Automatic>();
    static_assert(automatic.enumCount() == 3);
    Entries a, b;
    expect(automatic.describeEnum(&a, Entries::sink), "automatic full traversal");
    for (unsigned i = 0; i != 3; ++i) {
        expect(automatic.describeEnumEntry(i, &b, Entries::sink), "automatic indexed traversal");
        expect(a.values[i].get<std::int32_t>() == b.values[i].get<std::int32_t>(), "automatic codes match");
    }
    expect(a.names == b.names, "automatic names match");
}

CommandResult target(float, Wide, std::uint16_t) noexcept { return CommandResult::Executed; }
CommandResult reset() noexcept { return CommandResult::Executed; }
constexpr CommandTable commands{
    command<&target>("run", arg<0>("", "", 1.f, 0.f, 2.f),
        arg<1>("Mode", "", enumSpec<Wide::Large, Wide::Small>())),
    command<&reset>("reset"), reservedCommand()};
static_assert(commands[0].parameterCount() == 3 && commands[0].hasDescription());
static_assert(commands[1].parameterCount() == 0 && commands[1].hasDescription());
static_assert(commands[2].parameterCount() == 0 && !commands[2].hasDescription());
unsigned functionVisits = 0;
bool functionVisitor(const CommandParam&) noexcept { ++functionVisits; return true; }

void parameters()
{
    std::array<CommandParam, 3> all{};
    unsigned visits = 0;
    expect(commands[0].forEachParameter([&](const CommandParam& p) noexcept {
        all[visits++] = p; return true;
    }) && visits == 3, "full parameter visitor remains ordered");
    for (std::uint32_t i = 0; i < 3; ++i) {
        expect(commands[0].visitParameter(i, [&](const CommandParam& p) noexcept {
            ++visits;
            return p.index == i && p.name == all[i].name && p.unit == all[i].unit &&
                static_cast<ScalarType>(p.type) == static_cast<ScalarType>(all[i].type) &&
                p.type.enumCount() == all[i].type.enumCount();
        }), "indexed parameter agrees with whole traversal");
    }
    expect(visits == 6 && all[0].name != nullptr && all[0].name[0] == '\0' &&
           all[2].name == nullptr && all[2].unit == nullptr, "empty and absent labels stay distinct");
    expect(all[1].type.enumCount() == 2, "parameter enum metadata remains accessible");
    expect(!commands[0].describeParameter(0, nullptr, nullptr) &&
           !commands[0].visitParameter(3, functionVisitor) &&
           !commands[0].visitParameter(UINT32_MAX, functionVisitor) && functionVisits == 0,
           "invalid index/null sink never invokes visitor");
    expect(commands[1].forEachParameter(functionVisitor) &&
           !commands[2].forEachParameter(functionVisitor) &&
           !commands[1].visitParameter(0, functionVisitor) && functionVisits == 0,
           "zero arity has description; reserved position does not");
    expect(commands[0].visitParameter(2, functionVisitor) && functionVisits == 1,
           "function reference is a supported indexed visitor");
    bool (*nullVisitor)(const CommandParam&) noexcept = nullptr;
    expect(!commands[0].visitParameter(0, nullVisitor), "null visitor rejected");
    struct Visitor {
        unsigned& calls;
        explicit Visitor(unsigned& n) : calls(n) {}
        Visitor(const Visitor&) = delete;
        bool operator()(const CommandParam&) const volatile noexcept { ++calls; return false; }
    };
    const volatile Visitor visitor{visits};
    expect(!commands[0].visitParameter(1, visitor) && visits == 7,
           "noncopyable cv visitor keeps qualifiers and false result");
    expect(commands[0].visitParameter(0, [&](const CommandParam&) noexcept {
        return commands[0].visitParameter(2, functionVisitor);
    }) && functionVisits == 2, "reentrant indexed access has independent contexts");
}
}
int main()
{
    enums(); parameters();
    std::printf("%d/%d indexed metadata checks passed\n", checks - failures, checks);
    return failures != 0;
}
