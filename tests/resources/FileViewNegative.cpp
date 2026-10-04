// Escaping facades and iterators must not borrow a temporary descriptor table.
// Authors: Ruslan Kovtun (shpegun60), codexAi. MIT.
// Checks that file views and iterators cannot be retained from temporary filesystem facades.
// Additional cases preserve private construction and explicit presence-testing contracts.

#include <resource/Resource.hpp>
#include <ranges>
#include <utility>

// Stable empty provider isolates facade and iterator lifetime refusal from provider validity.
// Public methods:
// - size(): Report empty extent.
// - read(): Report empty stream.
struct Provider {
	resource::FileSize size() const noexcept
	{
		return 0;
	}

	resource::ReadResult read(resource::Cursor cursor, resource::Output) const noexcept
	{
		return {resource::Status::Ok, cursor, 0, true};
	}
} provider;

const auto files = resource::filesystem(resource::file("/test", provider));

#if CASE == 1
auto bad = resource::filesystem(resource::file("/test", provider))[0];
#elif CASE == 2
auto bad = std::move(files)[0];
#elif CASE == 3
auto bad = resource::filesystem(resource::file("/test", provider)).begin();
#elif CASE == 4
auto bad = resource::filesystem(resource::file("/test", provider)).end();
#elif CASE == 5
auto bad = std::move(files).begin();
#elif CASE == 6
auto bad = std::move(files).end();
#elif CASE == 7
auto bad = std::ranges::begin(resource::filesystem(resource::file("/test", provider)));
#elif CASE == 8
auto bad = std::ranges::end(std::move(files));
#elif CASE == 9
bool bad = files[0];
#elif CASE == 10
auto bad = files[0].object;
#elif CASE == 11
auto bad = files[0].ops;
#elif CASE == 12
resource::FileView bad{nullptr, 0};
#elif CASE == 13
resource::FileIterator bad{nullptr, 0};
#else
auto facade = files[0];
auto viewFacade = files.view()[0];
auto iterator = files.begin();
auto temporaryViewIterator = std::ranges::begin(files.view());
bool valid = static_cast<bool>(facade);
static_assert(std::ranges::borrowed_range<resource::FileSystemView>);
static_assert(!std::ranges::borrowed_range<decltype(files)>);
#endif
