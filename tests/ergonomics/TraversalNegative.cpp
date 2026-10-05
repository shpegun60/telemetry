/* Exact bool visitors, complete typed instantiation and borrowed table diagnostics. MIT. */
#include "../structured/traversal/Fixture.hpp"
#include <utility>

using namespace fixture;

struct BoolLike {
	constexpr operator bool() const noexcept
	{
		return true;
	}
};

// The actual borrowed group-name category determines the callback result.
struct NameCategoryVisitor {
	template<class Definition>
	bool operator()(std::string_view&&, const Definition&) const
	{
		return true;
	}

	template<class Definition>
	int operator()(const std::string_view&, const Definition&) const
	{
		return 1;
	}
};

int main()
{
#if CASE <= 18
#if (CASE - 1) % 6 == 0
	const auto& table = localFields;
#elif (CASE - 1) % 6 == 1
	const auto& table = localCommands;
#elif (CASE - 1) % 6 == 2
	const auto& table = localServices;
#elif (CASE - 1) % 6 == 3
	const auto& table = fields;
#elif (CASE - 1) % 6 == 4
	const auto& table = commands;
#else
	const auto& table = services;
#endif
#if CASE <= 6
	(void)table.forEachWhile([](auto&&...) {
	});
#elif CASE <= 12
	(void)table.forEachWhile([](auto&&...) {
		return 1;
	});
#else
	(void)std::move(table).forEachWhile([](auto&&...) {
		return true;
	});
#endif
#elif CASE <= 24
#if (CASE - 19) % 3 == 0
	const auto& table = fields;
#elif (CASE - 19) % 3 == 1
	const auto& table = commands;
#else
	const auto& table = services;
#endif
#if CASE <= 21
	std::move(table).forEachEntry([](auto&&...) {
	});
#else
	(void)std::move(table).forEachEntryWhile([](auto&&...) {
		return true;
	});
#endif
#elif CASE <= 27
#if CASE == 25
	const auto index = fields.index();
#elif CASE == 26
	const auto index = commands.index();
#else
	const auto index = services.index();
#endif
	(void)index.forEachEntryWhile([](auto&&...) {
		return 1;
	});
#elif CASE <= 30
#if CASE == 28
	const auto& table = localFields;
#elif CASE == 29
	const auto& table = localCommands;
#else
	const auto& table = localServices;
#endif
	(void)table.forEachWhile([]<std::size_t I>(const auto&) {
		if constexpr (I + 1 == std::remove_cvref_t<decltype(table)>::staticSize)
			return 1;
		else
			return false; // Runtime stops before the ill-formed final typed branch.
	});
#elif CASE <= 33
#if CASE == 31
	const auto& table = fields;
#elif CASE == 32
	const auto& table = commands;
#else
	const auto& table = services;
#endif
	(void)table.forEachWhile([]<std::size_t G, std::size_t I>(std::string_view, const auto&) {
		if constexpr (G == 2 && I == 0)
			return 1;
		else
			return false; // The first group stops, but later groups still instantiate.
	});
#elif CASE == 34
	bool result = true;
	(void)localFields.forEachWhile([&](const auto&) -> bool& {
		return result;
	});
#elif CASE == 35
	bool result = true;
	(void)commands.forEachWhile([&](std::string_view, const auto&) -> bool& {
		return result;
	});
#elif CASE == 36
	bool result = true;
	(void)services.index().forEachEntryWhile(
	    [&](ts::PackedId, std::string_view, const auto&) -> bool& {
		    return result;
	    });
#elif CASE == 37
	(void)localFields.get<1>().write(1.5);
#elif CASE == 38
	(void)localServices.forEachWhile([](const auto&) {
		return BoolLike{};
	});
#elif CASE == 39
	(void)fields.forEachWhile([](std::string_view, const auto&) {
		return BoolLike{};
	});
#elif CASE == 40
	(void)commands.index().forEachEntryWhile([](ts::PackedId, std::string_view, const auto&) {
		return BoolLike{};
	});
#elif CASE == 41
	(void)fields.forEachWhile(NameCategoryVisitor{});
#else
#error Unknown negative case
#endif
}
