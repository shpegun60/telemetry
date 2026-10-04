/* Exact types, bounds, conversions and borrowed iteration. MIT. */
#include "Fixture.hpp"
#include <cassert>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>

using namespace fixture;
using telemetry::makeId;
using std::string_view;

// Move-only visitor proves traversal borrows one visitor instead of copying it per row.
// API: MoveOnlyVisitor(), operator().
struct MoveOnlyVisitor {
	unsigned calls = 0;
	MoveOnlyVisitor() = default;
	MoveOnlyVisitor(const MoveOnlyVisitor&) = delete;

	template<class Definition>
	void operator()(const Definition&)
	{
		++calls;
	}
};

static_assert(
    std::is_same_v<
        decltype(localFields.get<Position::U16>()),
        const std::tuple_element_t<
            1, std::tuple<decltype(ts::field<&Device::readBool, &Device::setBool>("x", device)),
                          decltype(ts::field<&Device::readU16, &Device::setU16>("x", device))>>&>);
static_assert(&fields.get<makeId<2, 7>()>() == &localFields.get<7>());
static_assert(&commands.get<makeId<2, 0>()>() == &localCommands.get<0>());
static_assert(&services.get<makeId<2, 0>()>() == &localServices.get<0>());
static_assert(noFields.empty() && noCommands.empty() && noServices.empty());
static_assert(noFields.begin() == noFields.end());
static_assert(noCommands.begin() == noCommands.end());
static_assert(noServices.begin() == noServices.end());
static_assert(noFieldCatalogs.empty() && noCommandCatalogs.empty() && noServiceCatalogs.empty());
static_assert([] {
	std::size_t entries = 0;
	localFields.forEach([&]<std::size_t I>(const auto& endpoint) {
		if (I != entries || endpoint.name() == nullptr)
			std::abort();
		++entries;
	});
	fields.forEach([&]<std::size_t G, std::size_t I>(string_view name, const auto& endpoint) {
		if (name != (G == 0 ? "first" : "repeat") || I >= 10 || endpoint.name() == nullptr)
			std::abort();
		++entries;
	});
	return entries == 30;
}());

template<class Table>
void checkLocal(const Table& table)
{
	std::size_t count = 0;
	table.forEach([&]<std::size_t I>(const auto& endpoint) {
		assert(I == count++);
		assert(endpoint.name() == table[I].name);
		unsigned calls = 0;
		auto visitor = [&](const auto& selected) {
			++calls;
			assert(static_cast<const void*>(&selected) == &endpoint);
		};
		assert(table.visit(I, visitor) && calls == 1);
	});
	assert(count == table.size());
	count = 0;
	table.forEach([&](const auto&) {
		++count;
		return false;
	}); // Results are ignored.
	assert(count == table.size());
	count = 0;
	for (const auto& entry : table)
		assert(&entry == table.data() + count++);
	assert(count == table.size());
}

template<class Table>
void checkGlobal(const Table& table)
{
	unsigned visits = 0;
	table.forEach([&]<std::size_t G, std::size_t I>(string_view name, const auto& endpoint) {
		assert(name == table[G].name && endpoint.name() == table[G].entries[I].name);
		auto selected = [&](const auto& definition) {
			++visits;
			assert(static_cast<const void*>(&definition) == &endpoint);
		};
		assert(table.visit(makeId<G, I>(), selected));
	});
	unsigned callbacks = 0;
	auto nothing = [&](const auto&) {
		++callbacks;
	};
	for (const auto id :
	     {std::uint64_t{UINT32_MAX}, std::uint64_t{1} << 32, (std::uint64_t{1} << 32) + 1})
		assert(!table.visit(id, nothing));
	assert(!table.visit(-1, nothing));
	assert(!table.visit(makeId<1, 0>(), nothing));
	assert(!table.visit(makeId<3, 0>(), nothing));
	assert(callbacks == 0);
	unsigned groups = 0;
	for (const auto& group : table)
		assert(&group == table.data() + groups++);
	assert(groups == 3 && visits == 2 * table[0].count);
}

void conversions()
{
	constexpr auto u16 = makeId<2, 1>();
	assert((localFields.readAs<float, 1>() == 65535.0f));
	assert(fields.readAs<float>(u16) == 65535.0f);
	assert(fields.writeAs(u16, 12.75) == W::Applied && device.integer == 12);
	assert((fields.writeAs<u16>(-0.5) == W::Applied && device.integer == 0));
	const auto writes = device.writes;
	for (const auto value : {-1.0, 65536.0, std::numeric_limits<double>::infinity(),
	                         std::numeric_limits<double>::quiet_NaN()})
		assert(fields.writeAs(u16, value) == W::InvalidValue);
	assert(device.writes == writes);
	assert(fields.writeAs(u16, UINT64_MAX) == W::InvalidValue);
	assert(fields.writeAs(makeId<0, 4>(), INT64_C(-1)) == W::InvalidValue);
	assert(!fields.readAs<std::uint64_t>(makeId<0, 3>()));
	assert(!fields.readAs<std::int64_t>(makeId<0, 4>()));
	assert(fields.readAs<std::uint64_t>(makeId<0, 4>()) == UINT64_MAX);
	assert(fields.readAs<std::int64_t>(makeId<0, 3>()) == INT64_MIN);
	assert(fields.readAs<double>(makeId<0, 4>()) == static_cast<double>(UINT64_MAX));
	assert(fields.readAs<std::int16_t>(makeId<0, 5>()) == 2);
	assert(fields.writeAs(makeId<0, 5>(), -2) == W::Applied);
	assert(fields.readAs<Mode>(makeId<0, 5>()) == static_cast<Mode>(-2));
	assert(fields.readAs<float>(makeId<0, 5>()) == -2.0f);
	assert(fields.writeAs(makeId<0, 5>(), INT32_MAX) == W::InvalidValue);
	device.real = std::numeric_limits<double>::quiet_NaN();
	assert(!fields.readAs<bool>(makeId<0, 2>()) && !fields.readAs<std::uint32_t>(makeId<0, 2>()));
	assert(std::isnan(*fields.readAs<float>(makeId<0, 2>())));
	device.real = std::numeric_limits<double>::infinity();
	assert(std::isinf(*fields.readAs<float>(makeId<0, 2>())));
	device.real = std::numeric_limits<double>::max();
	assert(!fields.readAs<float>(makeId<0, 2>()));
	device.real = -0.0;
	assert(std::bit_cast<std::uint64_t>(*fields.readAs<double>(makeId<0, 2>())) == UINT64_C(1)
	                                                                                   << 63);
	assert(fields.writeAs(makeId<0, 0>(), -2) == W::Applied && device.boolean);
	assert(fields.writeAs(makeId<0, 0>(), std::numeric_limits<double>::quiet_NaN()) ==
	       W::InvalidValue);
	assert(!fields.readAs<float>(makeId<0, 7>()));
	const auto reads = device.reads;
	assert(!fields.readAs<Config>(makeId<0, 1>()));
	assert((!fields.readAs<std::array<std::uint16_t, 2>>(makeId<0, 7>())));
	assert(device.reads == reads); // Structural mismatch does not touch the source.
	assert(fields.writeAs(makeId<0, 7>(), std::array<std::uint16_t, 2>{}) == W::InvalidValue);
	assert(fields.writeAs(makeId<0, 6>(), 42) == W::ReadOnly);
	assert(fields.writeAs(makeId<0, 7>(), Config{99, false}) == W::Applied);
	assert(fields.readAs<Config>(makeId<0, 7>())->code == 99);
	assert(!fields.readAs<Config>(makeId<0, 9>()));
	assert(fields.writeAs(makeId<0, 9>(), Config{}) == W::Unavailable);
	slot.bind(device);
	assert(fields.readAs<Config>(makeId<0, 9>())->code == 99);
	assert(fields.writeAs(makeId<0, 9>(), Config{43, true}) == W::Applied);
	slot.reset();
	assert(!fields.readAs<float>(-1) && !fields.readAs<float>(UINT64_C(0x100000001)));
	assert(fields.writeAs(UINT64_C(0x100000001), 1) == W::NotFound);
	assert(fields.writeAs(makeId<1, 0>(), 1) == W::NotFound);
	assert(!localFields.readAs<float>(UINT64_C(0x100000001)));
	assert(localFields.writeAs(-1, 1) == W::NotFound);
	assert(localFields.readAs<double>(Position::Float) == -0.0);
	const auto big = fields.readAs<Big>(makeId<0, 8>());
	assert(big->words[2] == 3 && fields.writeAs(makeId<0, 8>(), *big) == W::Applied);
}

int main()
{
	// Traversal itself never calls a getter, command or service.
	checkLocal(localFields);
	checkLocal(localCommands);
	checkLocal(localServices);
	checkGlobal(fields);
	checkGlobal(commands);
	checkGlobal(services);
	assert(device.reads == 0 && device.writes == 0 && device.commands == 0 && device.services == 0);
	unsigned count = 0;
	auto never = [&](const auto&) {
		++count;
	};
	assert(!noFields.visit(0, never) && !noCommands.visit(0, never) && !noServices.visit(0, never));
	assert(!noFieldCatalogs.visit(0, never) && !noCommandCatalogs.visit(0, never) &&
	       !noServiceCatalogs.visit(0, never));
	noFields.forEach(never);
	noCommands.forEach(never);
	noServices.forEach(never);
	assert(count == 0 && !noFields.readAs<float>(0) && noFields.writeAs(0, 1) == W::NotFound);
	assert(!noFieldCatalogs.readAs<float>(0) && noFieldCatalogs.writeAs(0, 1) == W::NotFound);
	MoveOnlyVisitor visitor;
	localFields.forEach(visitor);
	assert(localCommands.visit(0, visitor) && visitor.calls == 11);
	assert(services.visit(makeId<2, 0>(), std::move(visitor)) && visitor.calls == 12);
#if defined(__cpp_exceptions)
	bool caught = false;
	try {
		(void)fields.visit(0, [](const auto&) {
			throw 42;
		});
	} catch (int value) {
		caught = value == 42;
	}
	assert(caught);
#endif
	conversions();
}
