/**
 * @file File.hpp
 * @brief C++20 provider contracts and constexpr non-owning file bindings.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see LICENSE.
 */
#pragma once
#include "Types.hpp"
#include <concepts>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>

namespace resource
{
template <class T>
concept SizedProvider = requires(const T& provider) {
    { provider.size() } noexcept -> std::same_as<FileSize>;
};
template <class T>
concept ReadableProvider = requires(const T& provider, Cursor cursor, Output output) {
    { provider.read(cursor, output) } noexcept -> std::same_as<ReadResult>;
};
template <class T>
concept WritableProvider = requires(T& provider, Cursor cursor, Input input, bool final) {
    { provider.write(cursor, input, final) } noexcept -> std::same_as<WriteResult>;
};
template <class T>
concept Provider =
    !std::is_volatile_v<T> && SizedProvider<T> && (ReadableProvider<T> || WritableProvider<T>);

struct FileOps
{
    FileSize (*size)(const void*) noexcept;
    ReadResult (*read)(const void*, Cursor, Output) noexcept;
    WriteResult (*write)(void*, Cursor, Input, bool) noexcept;
};

namespace detail
{
[[noreturn]] inline void invalidDefinition() noexcept
{
    std::abort();
}

// Paths are flat labels. These spelling checks never build or traverse a tree.
constexpr bool validPath(std::string_view path) noexcept
{
    if (path.size() < 2 || path.front() != '/' || path.back() == '/')
    {
        return false;
    }
    std::size_t start = 1;
    for (std::size_t i = 1; i <= path.size(); ++i)
    {
        if (i != path.size() && path[i] != '/')
        {
            if (static_cast<unsigned char>(path[i]) < 0x20 || path[i] == '\\')
            {
                return false;
            }
            continue;
        }
        const auto part = path.substr(start, i - start);
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
        start = i + 1;
    }
    return true;
}

template <Provider T>
inline constexpr FileOps operations = []
{
    // One immutable operation table per provider type. Missing capabilities
    // remain null; no type inspection takes place during a runtime request.
    FileOps ops{};
    ops.size = +[](const void* context) noexcept
    {
        return static_cast<const T*>(context)->size();
    };
    if constexpr (ReadableProvider<T>)
    {
        ops.read = +[](const void* context, Cursor cursor, Output output) noexcept
        {
            return static_cast<const T*>(context)->read(cursor, output);
        };
    }
    if constexpr (WritableProvider<T>)
    {
        ops.write = +[](void* context, Cursor cursor, Input input, bool final) noexcept
        {
            return static_cast<T*>(context)->write(cursor, input, final);
        };
    }
    return ops;
}();
} // namespace detail

// Only file() constructs descriptors. Strings and providers must outlive the
// filesystem; copying this descriptor never copies its provider.
class FileEntry
{
public:
    const std::string_view path;
    void* const object;
    const FileOps* const ops;
    constexpr FileEntry(const FileEntry&) noexcept = default;
    constexpr FileEntry(FileEntry&&) noexcept = default;
    FileEntry& operator=(const FileEntry&) = delete;
    FileEntry& operator=(FileEntry&&) = delete;

private:
    template <Provider T>
    friend constexpr FileEntry file(std::string_view, T&) noexcept;

    constexpr FileEntry(std::string_view label, void* context, const FileOps* functions) noexcept
        : path(label), object(context), ops(functions)
    {
    }
};

template <Provider T>
constexpr FileEntry file(std::string_view path, T& provider) noexcept
{
    if (!detail::validPath(path))
    {
        detail::invalidDefinition();
    }
    // Constness is retained in T and restored by the thunk. A const provider
    // can only expose write if that operation itself is callable on const T.
    return {path, const_cast<void*>(static_cast<const void*>(std::addressof(provider))),
            &detail::operations<T>};
}
template <class T>
FileEntry file(std::string_view, T&&) = delete;
// Reject owning temporary strings without pulling <string> into this header.
template <class Path, class T>
    requires(!std::is_lvalue_reference_v<Path> &&
             !std::is_same_v<std::remove_cvref_t<Path>, std::string_view> &&
             !std::is_pointer_v<std::remove_cvref_t<Path>> &&
             !std::is_array_v<std::remove_reference_t<Path>>)
FileEntry file(Path&&, T&) = delete;
} // namespace resource
