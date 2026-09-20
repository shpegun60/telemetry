// Policy metadata, positional traversal and synchronous value/parameter visitors.
#include "Telemetry.h"
#include "serialization/TelemetryJson.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace telemetry;
int checks = 0, failures = 0;
void expect(bool good, const char* message)
{
    ++checks;
    if (!good) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
enum class Mode : std::uint8_t { Off, Auto };
struct Settings {
    float value = 230.f;
    float read() const noexcept { return value; }
    WriteResult write(float v) noexcept { value = v; return WriteResult::Applied; }
    CommandResult configure(float v, Mode) noexcept { value = v; return CommandResult::Executed; }
    CommandResult reset() noexcept { value = 230.f; return CommandResult::Executed; }
} settings;
float freeRead() noexcept { return settings.value; }
WriteResult freeWrite(float value) noexcept { return settings.write(value); }
constexpr auto plain = field<&Settings::read, &Settings::write>(
    "Limit", "V", settings, limits(230.f, 0.f, 500.f));
constexpr auto saved = plain.withFlags(FieldFlag::Persistent);
static_assert(std::is_same_v<decltype(plain), decltype(saved)>);
static_assert(FieldFlags{}.empty() && FieldFlags{}.contains(FieldFlag::None));
static_assert((FieldFlag::Persistent | FieldFlag::None).value() == 1);
static_assert((FieldFlags::fromRaw(0x80) | FieldFlag::Persistent).contains(FieldFlag::Persistent));
static_assert(!std::is_convertible_v<std::uint32_t, FieldFlags>);
static_assert(!plain.materialize().persistent() && saved.materialize().persistent());
static_assert(saved.materialize().writable() && saved.materialize().has(FieldFlag::Persistent));
static_assert(saved.withFlags({}).materialize().flags().empty());

constexpr FieldTable localFields{saved, reservedField(), field("Counter", "", +[]() noexcept { return std::uint16_t{7}; })};
constexpr FieldTable<> emptyFields{};
constexpr FieldCatalogTable fields{group("empty", emptyFields), group("settings", localFields), group("tail", emptyFields)};
constexpr Catalog rawFields[]{Catalog{"empty", nullptr, 0}, Catalog{"settings", localFields.data(), localFields.size()}};
constexpr auto staticFields = CatalogIndex::bind<rawFields>();
constexpr CommandTable localCommands{
    command<&Settings::configure>("Configure", settings, arg<0>("Limit", "V", 230.f, 0.f, 500.f), arg<1>("Mode", "", Mode::Auto)),
    reservedCommand(), command<&Settings::reset>("Reset", settings)};
constexpr CommandTable<> emptyCommands{};
constexpr CommandCatalogTable commands{group("empty", emptyCommands), group("settings", localCommands), group("tail", emptyCommands)};

constexpr bool constantTraversal()
{
    std::size_t groups = 0, rows = 0;
    for (auto catalog : fields.catalogs()) {
        if (catalog.index() != groups++) return false;
        std::size_t index = 0;
        for (auto entry : catalog.fields()) {
            if (entry.index() != index || entry.id() != makeId(1, static_cast<EntryOffset>(index))
                || &entry.field() != localFields.data() + index) return false;
            ++rows; ++index;
        }
    }
    for (auto catalog : commands.catalogs()) {
        for (auto entry : catalog.commands()) {
            if (entry.id() != makeId(1, entry.index()) || &entry.command() != &localCommands[entry.index()]) return false;
        }
    }
    return groups == 3 && rows == 3;
}
static_assert(constantTraversal());
static_assert(Catalog{}.empty() && CommandCatalog{}.empty());
static_assert(CatalogIndex{}.catalogs().empty() && CommandCatalogIndex{}.catalogs().empty());
static_assert(emptyFields.begin() == emptyFields.end() && emptyCommands.empty());
static_assert(std::is_same_v<decltype(*localFields.begin()), const Field&>);
static_assert(std::is_same_v<decltype(*localCommands.begin()), const Command&>);
static_assert(std::is_same_v<decltype(*fields.begin()), const Catalog&>);
static_assert(std::is_same_v<decltype(*commands.begin()), const CommandCatalog&>);
static_assert(std::is_trivially_copyable_v<FieldEntryView> && std::is_trivially_copyable_v<CommandEntryView>);
static_assert(telemetryAbiVersion == 7);
static_assert(Field::abiFlagsOffset() == offsetof(Field, unit) + sizeof(const char*));
static_assert(offsetof(Field, set) == cacheLineBytes);
#if defined(__arm__)
static_assert(sizeof(Field) == 96 && sizeof(Command) == 20 && Field::abiFlagsOffset() == 20);
#endif

constexpr Scalar compileValue = Scalar::fromU16(23);
static_assert(compileValue.visit([](auto v) constexpr -> int {
    if constexpr (std::is_same_v<decltype(v), std::monostate>) return -1;
    else return static_cast<int>(v);
}) == 23);

void flagsAndSchema()
{
    expect(!Field{}.persistent() && !Field{}.writable() && Field{}.flags().empty(), "default field policy and capabilities");
    const auto flags = FieldFlags::fromRaw(UINT32_C(0x80000101));
    expect(flags.value() == UINT32_C(0x80000101) && flags.contains(FieldFlag::Persistent)
           && !flags.contains(FieldFlags::fromRaw(2)), "mask retains unknown bits and tests all requested bits");
    expect((flags | FieldFlag::None) == flags && flags != FieldFlags{}, "mask combination and comparison");
    const Field a[]{plain.materialize()}, b[]{saved.materialize()};
    const Catalog ca[]{Catalog{"settings", a}}, cb[]{Catalog{"settings", b}};
    char before[2048], after[2048];
    expect(writeSchema(ca, 1, before, sizeof before) && writeSchema(cb, 1, after, sizeof after)
           && std::strstr(before, "\"w\":true,\"f\":0") && std::strstr(after, "\"w\":true,\"f\":1"),
           "schema always separates numeric flags from capability");
    const auto baseHash = schemaCrc(ca, 1);
    std::array<std::uint32_t, 4> hashes{};
    for (unsigned byte = 0; byte < 4; ++byte) {
        const Field changed[]{plain.withFlags(FieldFlags::fromRaw(std::uint32_t{1} << (8 * byte))).materialize()};
        const Catalog catalogs[]{Catalog{"settings", changed}};
        hashes[byte] = schemaCrc(catalogs, 1);
        expect(hashes[byte] != baseHash, "every flags byte participates in the fingerprint");
        for (unsigned prior = 0; prior < byte; ++prior)
            expect(hashes[prior] != hashes[byte], "flag byte positions remain distinct in the fingerprint");
    }
    const Field raw[]{plain.withFlags(FieldFlags::fromRaw(UINT32_MAX)).materialize()};
    const Catalog rawCatalog[]{Catalog{"raw", raw}};
    expect(writeSchema(rawCatalog, 1, after, sizeof after) && std::strstr(after, "\"f\":4294967295"), "all 32 wire bits serialize unsigned");
    expect(writeValues(ca, 1, before, sizeof before) == writeValues(cb, 1, after, sizeof after)
           && !std::strcmp(before, after) && !std::strstr(after, "\"f\":"), "values format ignores field policy");
    const auto fingerprint = schemaCrc(cb, 1);
    expect(b[0].write(251.) == WriteResult::Applied && schemaCrc(cb, 1) == fingerprint,
           "flags do not affect writes and values do not affect schema");
    settings.value = 230.f;
}

template <class Read, class Write>
void slotPolicy(Read& read, Write& write)
{
    const FieldTable table{field("slot", "", read, write).withFlags(FieldFlag::Persistent)};
    expect(table[0].persistent() && table[0].writable(), "empty callable slots retain persistent capability");
    expect(!table.template read<0>() && table.template write<0>(1.f) == WriteResult::Unavailable,
           "policy does not turn empty slot targets into available data");
}
void bindingForms()
{
    const auto pointer = field("function", "", &freeRead, &freeWrite).withFlags(FieldFlag::Persistent);
    const auto nttp = field<&freeRead, &freeWrite>("template", "").withFlags(FieldFlag::Persistent);
    const auto lambda = field("lambda", "", []() noexcept { return 1.f; }, [](float) noexcept { return WriteResult::Applied; }).withFlags(FieldFlag::Persistent);
    auto get = [&]() noexcept { return settings.value; };
    auto set = [&](float v) noexcept { return settings.write(v); };
    const auto borrowed = field("borrowed", "", get, set).withFlags(FieldFlag::Persistent);
    const auto manual = field(Field{"manual", "", ScalarType::F32, &freeRead, nullptr});
    expect(pointer.materialize().persistent() && nttp.materialize().persistent()
           && lambda.materialize().persistent() && borrowed.materialize().persistent()
           && !manual.materialize().persistent(), "one builder covers every existing definition form");
    OwnerSlot<Settings> owner;
    const FieldTable ownerFields{field<&Settings::read, &Settings::write>("owner", "", owner).withFlags(FieldFlag::Persistent)};
    expect(ownerFields[0].persistent() && !ownerFields.read<0>() && ownerFields.write<0>(1) == WriteResult::Unavailable, "empty OwnerSlot supports persistent descriptors");
    owner.bind(settings);
    expect(ownerFields.read<0>() == settings.value && ownerFields.write<0>(231) == WriteResult::Applied, "rebinding retains policy and native access");
    FunctionSlot<float() noexcept> fr; FunctionSlot<WriteResult(float) noexcept> fw;
    ContextFunctionSlot<float() noexcept> cr; ContextFunctionSlot<WriteResult(float) noexcept> cw;
    DelegateRefSlot<float() noexcept> rr; DelegateRefSlot<WriteResult(float) noexcept> rw;
    DelegateSlot<float() noexcept> dr; DelegateSlot<WriteResult(float) noexcept> dw;
    slotPolicy(fr, fw); slotPolicy(cr, cw); slotPolicy(rr, rw); slotPolicy(dr, dw);
    settings.value = 230.f;
}

template <ScalarType Type>
void visitAlternative(Scalar value)
{
    using Expected = Scalar::NativeType<Type>;
    int visits = 0;
    const Scalar& constant = value;
    const bool matched = constant.visit([&](const auto& native) {
        ++visits;
        using T = std::decay_t<decltype(native)>;
        return std::is_same_v<T, Expected>;
    });
    expect(matched && visits == 1, "const visit exposes the exact active alternative once");
    value.visit([](auto& native) { native = std::decay_t<decltype(native)>{}; });
    expect(value.type() == Type && value.get<Expected>() == Expected{}, "mutable visit changes payload without changing tag");
}
void scalarVisits()
{
    visitAlternative<ScalarType::Null>({});
    visitAlternative<ScalarType::F32>(Scalar::fromF32(1.25f));
    visitAlternative<ScalarType::F64>(Scalar::fromF64(-2.5));
    visitAlternative<ScalarType::U8>(Scalar::fromU8(UINT8_MAX));
    visitAlternative<ScalarType::U16>(Scalar::fromU16(UINT16_MAX));
    visitAlternative<ScalarType::U32>(Scalar::fromU32(UINT32_MAX));
    visitAlternative<ScalarType::U64>(Scalar::fromU64(UINT64_MAX));
    visitAlternative<ScalarType::S8>(Scalar::fromS8(INT8_MIN));
    visitAlternative<ScalarType::S16>(Scalar::fromS16(INT16_MIN));
    visitAlternative<ScalarType::S32>(Scalar::fromS32(INT32_MIN));
    visitAlternative<ScalarType::S64>(Scalar::fromS64(INT64_MIN));
    visitAlternative<ScalarType::Bool>(Scalar::fromBool(true));
    Scalar value = 3.f;
    const float fallback = 0.f;
    const float& reference = std::as_const(value).visit([&](const auto& native) -> const float& {
        if constexpr (std::is_same_v<std::decay_t<decltype(native)>, float>) return native;
        else return fallback;
    });
    expect(&reference == value.getIf<float>(), "lvalue visit may return a borrowed reference");
#if defined(__cpp_exceptions)
    bool propagated = false;
    try { value.visit([](auto) -> void { throw 42; }); }
    catch (int n) { propagated = n == 42; }
    expect(propagated, "visitor exceptions are not hidden by an unconditional noexcept");
#endif
}

void traversal()
{
    expect(std::distance(localFields.begin(), localFields.end()) == 3
           && std::distance(localCommands.index().begin(), localCommands.index().end()) == 3, "raw local ranges expose const descriptors");
    std::size_t rows = 0, catalogs = 0, persistent = 0;
    for (auto catalog : fields.index().catalogs()) {
        expect(catalog.index() == catalogs++ && catalog.catalog().size() == catalog.fields().size(), "catalog view carries its actual group position");
        for (auto entry : catalog.fields()) {
            expect(entry.id() == makeId(1, static_cast<EntryOffset>(rows)) && &entry.field() == fields.find(entry.id()), "entry ID resolves to its original descriptor");
            if (entry.field().persistent()) ++persistent;
            if (entry.index() == 1) expect(entry.field().read().type() == ScalarType::Null, "reserved field occupies its original index");
            ++rows;
        }
    }
    expect(rows == 3 && catalogs == 3 && persistent == 1, "empty groups and reserved positions do not renumber traversal");
    auto copy = fields.catalogs();
    auto iterator = copy.begin();
    expect(iterator->index() == 0 && (iterator++)->index() == 0 && iterator->index() == 1, "iterator arrow and postincrement operate on value views");
    const auto entries = iterator->fields();
    expect(entries.begin()->id() == makeId(1, 0), "nested range outlives its temporary catalog view");
    rows = 0;
    for (const Catalog& catalog : staticFields) for (const Field& entry : catalog) { (void) entry; ++rows; }
    expect(rows == 3 && !staticFields.catalogs().empty(), "static index supports both raw and indexed traversal");
    rows = 0; catalogs = 0;
    for (auto catalog : commands.index().catalogs()) {
        expect(catalog.index() == catalogs++ && !std::strcmp(catalog.name(), commands[catalog.index()].name), "command catalogs are symmetric with fields");
        for (auto entry : catalog.commands()) {
            expect(entry.id() == makeId(1, static_cast<EntryOffset>(rows)) && &entry.command() == commands.find(entry.id()), "command view IDs preserve positions");
            if (entry.index() == 1) expect(entry.command().call() == CommandResult::Unavailable, "reserved command remains visible");
            ++rows;
        }
    }
    expect(rows == 3 && catalogs == 3, "command traversal includes empty groups");
    const CatalogIndex nullFields{nullptr, SIZE_MAX};
    const CommandCatalogIndex nullCommands{nullptr, SIZE_MAX};
    expect(nullFields.empty() && nullFields.begin() == nullFields.end() && nullFields.catalogs().begin() == nullFields.catalogs().end()
           && nullCommands.empty() && nullCommands.catalogs().empty() && CommandIndex{}.empty(), "null views normalize before iteration");
}

void parameterVisitors()
{
    std::size_t count = 0;
    std::array<CommandParam, 2> copies;
    const bool complete = localCommands[0].forEachParameter([&](const CommandParam& p) noexcept {
        if (p.index != count || count >= copies.size()) return false;
        copies[count++] = p;
        return true;
    });
    expect(complete && count == 2 && !std::strcmp(copies[0].name, "Limit")
           && copies[0].type.maximum().get<float>() == 500.f && copies[1].type.hasEnum(), "parameter visitor exposes types, limits and enum metadata in order");
    count = 0;
    expect(!localCommands[0].forEachParameter([&](const CommandParam&) noexcept { ++count; return false; })
           && count == 1, "false stops before the next parameter");
    expect(localCommands[2].forEachParameter([&](const CommandParam&) noexcept { ++count; return true; })
           && count == 1 && !localCommands[1].forEachParameter([](const CommandParam&) noexcept { return true; }), "zero arity succeeds; absent description reports false");
    struct Visitor {
        std::size_t& count;
        explicit Visitor(std::size_t& v) noexcept : count(v) {}
        Visitor(const Visitor&) = delete;
        bool operator()(const CommandParam&) const noexcept { ++count; return true; }
    } visitor{count};
    count = 0;
    expect(localCommands[0].forEachParameter(std::as_const(visitor)) && count == 2, "visitor is borrowed without copying or removing const");
    bool (*nullVisitor)(const CommandParam&) noexcept = nullptr;
    expect(!localCommands[0].forEachParameter(nullVisitor), "null function visitor is rejected before invocation");
    count = 0;
    expect(localCommands[0].forEachParameter([&](const CommandParam&) noexcept {
        return localCommands[0].forEachParameter([&](const CommandParam&) noexcept { ++count; return true; });
    }) && count == 4, "nested parameter traversals keep independent callback contexts");
}

template <class Row, class Group, class Index>
void maximumTraversal()
{
    std::vector<Row> rows(idComponentCapacity + 1);
    std::vector<Group> groups;
    groups.reserve(idComponentCapacity + 1);
    for (std::size_t i = 0; i <= idComponentCapacity; ++i)
        groups.emplace_back("group", i == idComponentCapacity - 1 ? rows.data() : nullptr, rows.size());
    const Index index{groups.data(), groups.size()};
    std::size_t groupCount = 0, rowCount = 0;
    std::uint32_t last = 0;
    bool ordered = true;
    for (auto catalog : index.catalogs()) {
        ordered = ordered && catalog.index() == groupCount++;
        const auto entries = [&] {
            if constexpr (std::is_same_v<Row, Field>) return catalog.fields();
            else return catalog.commands();
        }();
        for (auto entry : entries) {
            ordered = ordered && entry.index() == rowCount++;
            last = entry.id();
        }
    }
    expect(ordered && groupCount == idComponentCapacity && rowCount == idComponentCapacity
           && last == UINT32_MAX, "65536 groups/entries reach UINT32_MAX and terminate without wrapping");
}
} // namespace

int main(int argc, char** argv)
{
    // The host runner checks these definition errors in separate processes.
    if (argc > 1) {
        const auto policy = FieldFlags::fromRaw(static_cast<std::uint32_t>(argc - 1));
        if (!std::strcmp(argv[1], "readonly")) {
            const auto invalid = field<&freeRead>("invalid", "").withFlags(policy);
            (void) invalid;
        } else if (!std::strcmp(argv[1], "writeonly")) {
            const Field invalid{"invalid", "", ScalarType::F32, nullptr, &freeWrite, policy};
            (void) invalid;
        } else return 2;
        return 0;
    }
    flagsAndSchema(); bindingForms(); scalarVisits(); traversal(); parameterVisitors();
    maximumTraversal<Field, Catalog, CatalogIndex>();
    maximumTraversal<Command, CommandCatalog, CommandCatalogIndex>();
    std::printf("%d/%d traversal/metadata checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
