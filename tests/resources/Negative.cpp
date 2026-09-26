// Compile-time contract rejections. Authors: Ruslan Kovtun, codexAi (MIT).
#include <resource/FileSystem.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

struct Provider
{
    resource::FileSize value = 0;

    resource::FileSize size() const noexcept
    {
        return value;
    }

    resource::ReadResult read(resource::Cursor, resource::Output) const noexcept
    {
        return {};
    }
} provider;

struct Derived : Provider
{
} derived;

struct Holder
{
    Provider value;

    operator const Provider&() const noexcept
    {
        return value;
    }
};

struct Proxy
{
    operator Provider() const noexcept
    {
        return {};
    }
} proxy;

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
auto bad = resource::file<const Provider>("/converted-temporary", Holder{});
#elif CASE == 14
auto bad = resource::file<const Provider>("/converted-lvalue", proxy);
#elif CASE == 15
auto bad = resource::file<Provider>(std::string{"/explicit-temporary-path"}, provider);
#elif CASE == 16
auto bad = resource::file<const Provider>(std::string{"/explicit-base-path"}, derived);
#elif CASE == 17
std::string prefix = "/prefix";
auto bad = resource::file<const Provider>(prefix + "/name", derived);
#elif CASE == 18
const std::string makePath();
auto bad = resource::file<const Provider>(makePath(), provider);
#elif CASE == 19
std::string path = "/moved-path";
auto bad = resource::file<Provider>(std::move(path), provider);
#elif CASE == 20
auto bad = resource::file<const Provider>("/empty-braces", {});
#elif CASE == 21
auto bad = resource::file<const Provider>("/braced-temporary", {Provider{}});
#elif CASE == 22
auto bad = resource::file<const Provider>("/braced-derived", {Derived{}});
#elif CASE == 23
auto bad = resource::file<const Provider>("/braced-reference-conversion", {Holder{}});
#elif CASE == 24
auto bad = resource::file<const Provider>("/braced-lvalue-conversion", {proxy});
#elif CASE == 25
auto bad = resource::file<const Provider>("/braced-value-conversion", {Proxy{}});
#elif CASE == 26
auto bad = resource::file<const Provider>(std::string{"/braced-provider-path"}, {provider});
#elif CASE == 27
auto bad = resource::file<const Provider>(std::string{"/braced-derived-path"}, {derived});
#elif CASE == 28
auto bad = resource::file<const Provider>("/explicit-temporary", Provider{});
#elif CASE == 29
auto bad = resource::file<const Provider>("/explicit-derived", Derived{});
#elif CASE == 30
auto bad = resource::file({std::string{"/braced-owning-path"}}, provider);
#elif CASE == 31
auto bad = resource::file<Provider>({std::string{"/explicit-braced-path"}}, provider);
#elif CASE == 32
auto bad = resource::file<const Provider>({std::string{"/braced-path-and-base"}}, {derived});
#elif CASE == 33
std::string path = "/braced-lvalue-path";
auto bad = resource::file<Provider>({path}, provider);
#elif CASE == 34
struct PathProxy
{
    std::string path = "/converted-braced-path";

    operator std::string_view() const noexcept
    {
        return path;
    }
};

auto bad = resource::file<const Provider>({PathProxy{}}, derived);
#else
constexpr auto good = resource::filesystem(resource::file("/good", provider));
static_assert(good.fileCount() == 1);
constexpr auto goodBase = resource::file<const Provider>("/base", derived);
static_assert(goodBase.path == "/base");
static_assert(goodBase.ops == &resource::detail::operations<const Provider>);
constexpr auto goodBracedBase = resource::file<const Provider>("/braced-base", {derived});
static_assert(goodBracedBase.object == goodBase.object && goodBracedBase.ops == goodBase.ops);

int main()
{
    unsigned checks = 0;
    auto check = [&checks](std::string_view path, const Provider& object, resource::FileEntry entry,
                           const resource::FileOps* operations)
    {
        ++checks;
        if (entry.path != path || entry.path.data() != path.data() ||
            entry.object != std::addressof(object) || entry.ops != operations ||
            entry.ops->size(entry.object) != object.size())
        {
            std::abort();
        }
    };
    provider.value = 11;
    derived.value = 23;
    const Provider constant{37};
    const Derived constantDerived{{41}};
    constexpr char literal[] = "/literal";
    const char* pointer = literal;
    char array[] = "/array";
    std::string path = "/lvalue-string-path-with-storage-beyond-the-small-string-buffer";
    const std::string constantPath = "/const-lvalue-string-path-with-stable-storage";
    const std::string_view view = path;
    const auto* mutableOps = &resource::detail::operations<Provider>;
    const auto* constantOps = &resource::detail::operations<const Provider>;

    check(literal, provider, resource::file(literal, provider), mutableOps);
    check(literal, provider, resource::file<Provider>(literal, provider), mutableOps);
    check(literal, provider, resource::file<const Provider>(literal, provider), constantOps);
    check(pointer, provider, resource::file(pointer, provider), mutableOps);
    check(pointer, provider, resource::file<Provider>(pointer, provider), mutableOps);
    check(pointer, provider, resource::file<Provider>(static_cast<const char*>(literal), provider),
          mutableOps);
    check(array, provider, resource::file<Provider>(array, provider), mutableOps);
    check(path, provider, resource::file(path, provider), mutableOps);
    check(path, provider, resource::file<Provider>(path, provider), mutableOps);
    check(constantPath, provider, resource::file<Provider>(constantPath, provider), mutableOps);
    check(view, provider, resource::file<Provider>(view, provider), mutableOps);
    check(view, provider, resource::file<Provider>(std::string_view{path}, provider), mutableOps);
    check(view, provider, resource::file<Provider>(std::move(view), provider), mutableOps);
    check(path, derived, resource::file<Provider>(path, derived), mutableOps);
    check(path, derived, resource::file<const Provider>(path, derived), constantOps);
    check(path, constant, resource::file(path, constant), constantOps);
    check(path, constant, resource::file<const Provider>(path, constant), constantOps);
    check(path, constantDerived, resource::file<const Provider>(path, constantDerived),
          constantOps);
    check(path, provider, resource::file<Provider>(path, {provider}), mutableOps);
    check(path, provider, resource::file<const Provider>(path, {provider}), constantOps);
    check(path, constant, resource::file<const Provider>(path, {constant}), constantOps);
    check(path, derived, resource::file<Provider>(path, {derived}), mutableOps);
    check(path, derived, resource::file<const Provider>(path, {derived}), constantOps);
    check(path, constantDerived, resource::file<const Provider>(path, {constantDerived}),
          constantOps);
    check(literal, provider, resource::file({literal}, provider), mutableOps);
    check(literal, provider, resource::file<Provider>({literal}, provider), mutableOps);
    check(literal, derived, resource::file<const Provider>({literal}, {derived}), constantOps);
    check(pointer, provider, resource::file<Provider>({pointer}, provider), mutableOps);
    check(pointer, derived, resource::file<const Provider>({pointer}, {derived}), constantOps);
    check(array, provider, resource::file<Provider>({array}, {provider}), mutableOps);
    check(array, derived, resource::file<const Provider>({array}, {derived}), constantOps);
    check(view, provider, resource::file<Provider>({view}, provider), mutableOps);
    check(view, provider, resource::file<Provider>({std::string_view{path}}, provider), mutableOps);
    check(view, derived, resource::file<const Provider>({view}, {derived}), constantOps);
    std::printf("Resource file bindings: %u controls passed\n", checks);
}
#endif
