// Compile-time contract rejections. Authors: Ruslan Kovtun, codexAi (MIT).
#include <resource/FileSystem.hpp>
#include <string>

struct Provider
{
    resource::FileSize size() const noexcept
    {
        return 0;
    }

    resource::ReadResult read(resource::Cursor, resource::Output) const noexcept
    {
        return {};
    }
} provider;
#if CASE == 1
auto bad = resource::file("/temporary", Provider{});
#elif CASE == 2
constexpr auto bad =
    resource::filesystem(resource::file("/same", provider), resource::file("/same", provider));
#elif CASE == 3
constexpr auto bad = resource::file("relative", provider);
#elif CASE == 4
constexpr auto bad = resource::file("/trailing/", provider);
#elif CASE == 5
constexpr auto bad = resource::file("/empty//part", provider);
#elif CASE == 6
constexpr auto bad = resource::file("/a/../b", provider);
#elif CASE == 7
auto bad = resource::filesystem(resource::file("/temporary-view", provider)).view();
#elif CASE == 8
auto bad = resource::file(std::string{"/temporary-path"}, provider);
#elif CASE == 9
struct Missing
{
    resource::FileSize size() const noexcept
    {
        return 0;
    }
} missing;

auto bad = resource::file("/missing", missing);
#elif CASE == 10
struct Throwing
{
    resource::FileSize size() const
    {
        return 0;
    }

    resource::ReadResult read(resource::Cursor, resource::Output) const noexcept
    {
        return {};
    }
} throwing;

auto bad = resource::file("/throwing", throwing);
#elif CASE == 11
struct Wide
{
    std::uint64_t size() const noexcept
    {
        return 0;
    }

    resource::ReadResult read(resource::Cursor, resource::Output) const noexcept
    {
        return {};
    }
} wide;

auto bad = resource::file("/narrowing", wide);
#elif CASE == 12
constexpr auto bad = resource::file("/control\x7f", provider);
#elif CASE == 13
struct Holder {
    Provider value;
    operator const Provider&() const noexcept { return value; }
};
auto bad = resource::file<const Provider>("/converted-temporary", Holder{});
#elif CASE == 14
struct Proxy { operator Provider() const noexcept { return Provider{}; } } proxy;
auto bad = resource::file<const Provider>("/converted-lvalue", proxy);
#else
constexpr auto good = resource::filesystem(resource::file("/good", provider));
static_assert(good.fileCount() == 1);
struct Derived : Provider {};
Derived derived;
constexpr auto goodBase = resource::file<const Provider>("/base", derived);
static_assert(goodBase.path == "/base");
#endif
