/**
 * @file TelemetryIndex.h
 * @brief Direct catalog lookup and runtime or compile-time typed access.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_INDEX_H
#define TELEMETRY_INDEX_H

#include "TelemetryCatalog.h"
#include "../core/TelemetryCompiler.h"

namespace telemetry {

template <const auto& Catalogs> class StaticCatalogIndex;

// Non-owning view of zero-based, densely numbered groups. Each Catalog has
// already validated its field prefix. Construction clips the group prefix
// at its first wrong ID; lookup never repeats validation or scans any rows.
// Catalogs, fields and their metadata must remain unchanged at stable
// addresses for the view's lifetime. Bound source values may change.
class CatalogIndex {
public:
    constexpr CatalogIndex() noexcept = default;

    // Bind a constexpr catalog array into the returned index's type. That
    // index supports both runtime IDs and read<Id>() with an inferred type.
    template <const auto& Catalogs>
    static constexpr StaticCatalogIndex<Catalogs> bind() noexcept
    {
        return {};
    }

    // See Catalog: prefer the deleted array-rvalue overload over decay.
    template <class = void>
    constexpr CatalogIndex(const Catalog* catalogs, std::size_t requestedCount) noexcept
        : catalogs_(catalogs)
    {
        if (catalogs_ == nullptr) return;
        const std::size_t limit = requestedCount < idComponentCapacity
            ? requestedCount : idComponentCapacity;
        while (count_ < limit && catalogs_[count_].id == static_cast<GroupId>(count_)) {
            ++count_;
        }
    }

    template <std::size_t N>
    constexpr explicit CatalogIndex(const Catalog (&catalogs)[N]) noexcept
        : CatalogIndex(static_cast<const Catalog*>(catalogs), N) {}

    template <std::size_t N>
    CatalogIndex(const Catalog (&&)[N]) = delete;
    template <std::size_t N>
    CatalogIndex(const Catalog (&&)[N], std::size_t) = delete;

    // Keep the two bounds checks at the call site even with -Os.
    TELEMETRY_FORCE_INLINE constexpr const Field* find(FieldId id) const noexcept
    {
        const std::size_t group = groupOf(id);
        if (group >= count_) return nullptr;
        const Catalog& entry = catalogs_[group];
        const std::size_t index = indexOf(id);
        return index < entry.count ? entry.fields + index : nullptr;
    }

    constexpr const Catalog* catalog(GroupId group) const noexcept
    {
        return group < count_ ? catalogs_ + group : nullptr;
    }

    [[nodiscard]] TELEMETRY_FORCE_INLINE Scalar read(FieldId id) const noexcept
    {
        const Field* const field = find(id);
        return field != nullptr ? field->read() : Scalar::null();
    }

    template <class T>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    auto read(FieldId id) const noexcept -> decltype(std::declval<const Field&>().template read<T>())
    {
        const Field* const field = find(id);
        return field != nullptr ? field->template read<T>() : std::nullopt;
    }

    // The same direct lookup and accepted prefixes serve reads and writes.
    // The view and its metadata stay const; only the bound owner is modified.
    template <class T>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    auto write(FieldId id, T value) const noexcept
        -> decltype(std::declval<const Field&>().write(value))
    {
        const Field* const field = find(id);
        return field != nullptr ? field->write(value) : WriteResult::NotFound;
    }

    constexpr const Catalog* data() const noexcept { return catalogs_; }
    constexpr std::size_t size() const noexcept { return count_; }

    static constexpr std::size_t abiCatalogsOffset() noexcept;
    static constexpr std::size_t abiCountOffset() noexcept;

private:
    const Catalog* catalogs_ = nullptr;
    std::size_t count_ = 0;
};

constexpr std::size_t CatalogIndex::abiCatalogsOffset() noexcept
{ return offsetof(CatalogIndex, catalogs_); }
constexpr std::size_t CatalogIndex::abiCountOffset() noexcept
{ return offsetof(CatalogIndex, count_); }

// A compile-time binding to one catalog array. All objects of this type use
// the same immutable view; there is no mutable base that could be rebound to
// a different schema. Referenced source values may still change at runtime.
template <const auto& Catalogs>
class StaticCatalogIndex {
    inline static constexpr CatalogIndex index_{Catalogs};
    // Instantiate the view when this type is formed, not only on its first
    // read. A runtime table must be rejected by bind itself.
    static_assert(index_.size() <= idComponentCapacity,
                  "StaticCatalogIndex requires a constexpr catalog array");

public:
    constexpr StaticCatalogIndex() noexcept = default;

    // Reuse the existing serialization and other CatalogIndex consumers.
    constexpr operator const CatalogIndex&() const noexcept { return index_; }

    TELEMETRY_FORCE_INLINE static constexpr const Field* find(FieldId id) noexcept
    {
        return index_.find(id);
    }

    static constexpr const Catalog* catalog(GroupId group) noexcept { return index_.catalog(group); }
    static constexpr const Catalog* data() noexcept { return index_.data(); }
    static constexpr std::size_t size() noexcept { return index_.size(); }

    [[nodiscard]] TELEMETRY_FORCE_INLINE static Scalar read(FieldId id) noexcept
    {
        return index_.read(id);
    }

    template <class T>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    static auto read(FieldId id) noexcept -> decltype(index_.template read<T>(id))
    {
        return index_.template read<T>(id);
    }

    template <FieldId Id>
    [[nodiscard]] TELEMETRY_FORCE_INLINE static auto read() noexcept
    {
        constexpr const Field* field = index_.find(Id);
        static_assert(field != nullptr,
                      "The read ID must belong to the catalog's accepted prefix");
        if constexpr (field != nullptr) {
            constexpr ScalarType type = field->declaredType;
            constexpr bool numeric = type != ScalarType::Null
                && static_cast<std::size_t>(type) < Scalar::typeCount;
            static_assert(numeric, "An inferred read requires a numeric or Bool declaredType");
            if constexpr (numeric) {
                using T = Scalar::NativeType<type>;
                return field->template read<T>();
            }
        }
    }

    template <class T>
    [[nodiscard]] TELEMETRY_FORCE_INLINE
    static auto write(FieldId id, T value) noexcept -> decltype(index_.write(id, value))
    {
        return index_.write(id, value);
    }
};

} // namespace telemetry

#endif
