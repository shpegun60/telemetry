/*
 * @file Registry.hpp
 * @brief Compile-time, dependency-first registry of exact C++ wire types.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_STRUCTURED_TYPE_REGISTRY_HPP
#define TELEMETRY_STRUCTURED_TYPE_REGISTRY_HPP

#include "Descriptor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace telemetry::structured {
namespace detail {

template <class... T>
struct TypeList {
    static constexpr std::size_t size = sizeof...(T);
};

template <class... Lists>
struct ConcatLists;

template <>
struct ConcatLists<> { using type = TypeList<>; };

template <class... First, class... Rest, class... Tail>
struct ConcatLists<TypeList<First...>, TypeList<Rest...>, Tail...>
    : ConcatLists<TypeList<First..., Rest...>, Tail...> {};

template <class... T>
struct ConcatLists<TypeList<T...>> { using type = TypeList<T...>; };

template <class List>
struct RegistryFromList;

template <class List, class T>
struct Contains;

template <class... Present, class T>
struct Contains<TypeList<Present...>, T>
    : std::bool_constant<(std::is_same_v<Present, T> || ...)> {};

template <class List, class T>
struct Append;

template <class... Present, class T>
struct Append<TypeList<Present...>, T> {
    using type = TypeList<Present..., T>;
};

template <class T, class... Present>
struct Position;

template <class T, class First, class... Rest>
struct Position<T, First, Rest...> {
    static constexpr TypeId value = [] {
        if constexpr (std::is_same_v<T, First>)
            return TypeId{0};
        else
            return TypeId{1} + Position<T, Rest...>::value;
    }();
};

template <class T>
struct Position<T> {
    static_assert(!std::is_same_v<T, T>, "Type is absent from this TypeRegistry");
};

template <class List, class T>
struct Collect;

template <class List, class T, std::size_t Index, std::size_t Count>
struct CollectMembers {
    using Member = typename Type<T>::template Member<Index>;
    using WithMember = typename Collect<List, typename Member::Native>::type;
    using type = typename CollectMembers<WithMember, T, Index + 1, Count>::type;
};

template <class List, class T, std::size_t Count>
struct CollectMembers<List, T, Count, Count> {
    using type = List;
};

template <class List, class T, TypeKind Kind = Type<T>::kind>
struct CollectDependencies {
    using type = List;
};

template <class List, class T>
struct CollectDependencies<List, T, TypeKind::Enum> {
    using type = typename Collect<List, typename Type<T>::Underlying>::type;
};

template <class List, class T>
struct CollectDependencies<List, T, TypeKind::Array> {
    using type = typename Collect<List, typename Type<T>::Element>::type;
};

template <class List, class T>
struct CollectDependencies<List, T, TypeKind::Struct> {
    using type = typename CollectMembers<List, T, 0, Type<T>::memberCount>::type;
};

template <class List, class T, bool Present = Contains<List, T>::value>
struct CollectOne {
    using type = List;
};

template <class List, class T>
struct CollectOne<List, T, false> {
    // Force all of Type<T>'s representation, depth and byte-limit checks even
    // when a caller only asks for the registry's type order.
    static_assert(Type<T>::wireSize <= Limits::maxValueWireBytes);
    using WithDependencies = typename CollectDependencies<List, T>::type;
    using type = typename Append<WithDependencies, T>::type;
};

template <class List, class T>
struct Collect : CollectOne<List, std::remove_cvref_t<T>> {};

template <class List, class... Roots>
struct CollectRoots;

template <class List>
struct CollectRoots<List> {
    using type = List;
};

template <class List, class First, class... Rest>
struct CollectRoots<List, First, Rest...> {
    using WithFirst = typename Collect<List, First>::type;
    using type = typename CollectRoots<WithFirst, Rest...>::type;
};

// These exact positions are part of the descriptor v3.0 contract.
using Builtins = TypeList<
    Void, bool, std::uint8_t, std::int8_t, std::uint16_t, std::int16_t,
    std::uint32_t, std::int32_t, std::uint64_t, std::int64_t, float, double>;

} // namespace detail

// Roots are supplied in the same first-use order as the eventual Model:
// Fields, Commands, then Services (request before response). A local table
// cannot assign global TypeIds before all roots are known.
template <class... Roots>
class TypeRegistry {
    using Ordered = typename detail::CollectRoots<detail::Builtins, Roots...>::type;

public:
    static_assert(Ordered::size <= Limits::maxTypeCount,
                  "Type registry exceeds model ceiling");
    static constexpr std::uint32_t typeCount = static_cast<std::uint32_t>(Ordered::size);

    template <class T>
    [[nodiscard]] static consteval TypeId typeId() noexcept
    {
        using Normal = std::remove_cvref_t<T>;
        if constexpr (detail::Contains<Ordered, Normal>::value) {
            return []<class... Present>(detail::TypeList<Present...>) {
                return detail::Position<Normal, Present...>::value;
            }(Ordered{});
        } else {
            static_assert(detail::Contains<Ordered, Normal>::value,
                          "Type is absent from this TypeRegistry");
            return TypeId{0};
        }
    }

private:
    template <class T>
    struct Stored {
        inline static constexpr auto members = [] {
            if constexpr (Type<T>::kind == TypeKind::Struct) {
                return []<std::size_t... I>(std::index_sequence<I...>) {
                    return std::array<MemberDescriptor, sizeof...(I)>{
                        MemberDescriptor{
                            typeId<typename Type<T>::template Member<I>::Native>(),
                            Type<T>::template Member<I>::name}...};
                }(std::make_index_sequence<Type<T>::memberCount>{});
            } else {
                return std::array<MemberDescriptor, 0>{};
            }
        }();

        inline static constexpr auto enumEntries = [] {
            if constexpr (Type<T>::kind == TypeKind::Enum) {
                using Raw = typename Type<T>::Underlying;
                using Unsigned = std::make_unsigned_t<Raw>;
                return []<std::size_t... I>(std::index_sequence<I...>) {
                    return std::array<EnumEntryDescriptor, sizeof...(I)>{
                        EnumEntryDescriptor{
                            static_cast<std::uint64_t>(static_cast<Unsigned>(
                                static_cast<Raw>(reflection::Enum<T>::template entryValue<I>()))),
                            reflection::Enum<T>::template entryName<I>()}...};
                }(std::make_index_sequence<reflection::Enum<T>::entryCount>{});
            } else {
                return std::array<EnumEntryDescriptor, 0>{};
            }
        }();

        [[nodiscard]] static consteval std::uint64_t recordSize() noexcept
        {
            // Eight bytes of record header and twelve bytes of type prefix.
            std::uint64_t bytes = 20;
            if constexpr (Type<T>::kind == TypeKind::Scalar) {
                bytes += 4;
            } else if constexpr (Type<T>::kind == TypeKind::Enum) {
                bytes += 8;
                for (const auto& entry : enumEntries)
                    bytes += Type<T>::wireSize + 4 + entry.name.size();
            } else if constexpr (Type<T>::kind == TypeKind::Struct) {
                bytes += 4;
                for (const auto& member : members)
                    bytes += 8 + member.name.size();
            } else if constexpr (Type<T>::kind == TypeKind::Array) {
                bytes += 8;
            }
            return bytes;
        }

        static constexpr std::uint64_t wideRecordBytes = recordSize();
        static_assert(wideRecordBytes <= std::numeric_limits<std::uint32_t>::max(),
                      "Type record byte count overflows u32");

        [[nodiscard]] static consteval TypeDescriptor make() noexcept
        {
            TypeDescriptor result{};
            result.id = typeId<T>();
            result.kind = Type<T>::kind;
            result.wireBytes = Type<T>::wireSize;
            result.recordBytes = static_cast<std::uint32_t>(wideRecordBytes);
            if constexpr (Type<T>::kind == TypeKind::Scalar) {
                result.scalarCode = Type<T>::code;
            } else if constexpr (Type<T>::kind == TypeKind::Enum) {
                result.relatedTypeId = typeId<typename Type<T>::Underlying>();
                result.enumData = enumEntries.empty() ? nullptr : enumEntries.data();
                result.enumCount = static_cast<std::uint32_t>(enumEntries.size());
            } else if constexpr (Type<T>::kind == TypeKind::Struct) {
                result.memberData = members.empty() ? nullptr : members.data();
                result.memberCount = static_cast<std::uint32_t>(members.size());
            } else if constexpr (Type<T>::kind == TypeKind::Array) {
                result.relatedTypeId = typeId<typename Type<T>::Element>();
                result.elementCount = static_cast<std::uint32_t>(Type<T>::count);
            }
            return result;
        }
    };

    template <class... Present>
    [[nodiscard]] static consteval auto makeDescriptors(detail::TypeList<Present...>) noexcept
    {
        return std::array<TypeDescriptor, sizeof...(Present)>{Stored<Present>::make()...};
    }

    inline static constexpr auto descriptors_ = makeDescriptors(Ordered{});

    [[nodiscard]] static consteval std::uint64_t totalBytes() noexcept
    {
        std::uint64_t result = 0;
        for (const auto& descriptor : descriptors_)
            result += descriptor.recordBytes;
        return result;
    }

    inline static constexpr std::uint64_t wideRecordsBytes_ = totalBytes();
    static_assert(wideRecordsBytes_ <= Limits::maxDescriptorBytes,
                  "Type records exceed descriptor byte ceiling");

    [[nodiscard]] static consteval std::uint64_t totalEnumEntries() noexcept
    {
        std::uint64_t result = 0;
        for (const auto& descriptor : descriptors_)
            result += descriptor.enumCount;
        return result;
    }

    static_assert(totalEnumEntries() <= Limits::maxEnumEntriesTotal,
                  "Enum dictionaries exceed model entry ceiling");

public:
    static constexpr std::uint32_t recordsBytes =
        static_cast<std::uint32_t>(wideRecordsBytes_);

    [[nodiscard]] static constexpr TypeRegistryView view() noexcept
    {
        return {descriptors_.data(), typeCount, recordsBytes};
    }

    template <TypeId Id>
    [[nodiscard]] static constexpr const TypeDescriptor& descriptor() noexcept
    {
        static_assert(Id < typeCount, "TypeId is outside this TypeRegistry");
        if constexpr (Id < typeCount)
            return descriptors_[Id];
        else
            return descriptors_[0]; // Unreachable after the assertion.
    }
};

namespace detail {

template <class... T>
struct RegistryFromList<TypeList<T...>> {
    using type = TypeRegistry<T...>;
};

} // namespace detail

} // namespace telemetry::structured

#endif
