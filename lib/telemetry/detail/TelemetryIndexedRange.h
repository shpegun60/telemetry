/**
 * @file TelemetryIndexedRange.h
 * @brief Borrowed positional ranges with value views and full 65536-entry bounds.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * License: MIT; see ../LICENSE.
 */
#ifndef TELEMETRY_INDEXED_RANGE_H
#define TELEMETRY_INDEXED_RANGE_H

#include "../core/TelemetryId.h"
#include <cstddef>
#include <iterator>
#include <memory>

namespace telemetry::detail {
// Dereference produces a small view by value, not a reference to iterator
// storage. Its descriptor still belongs to the original immutable table.
template <class View>
class IndexedViewIterator {
    using Descriptor = typename View::Descriptor;
    const Descriptor* rows_ = nullptr;
    // End may be 65536, which cannot be represented by an EntryOffset.
    std::size_t position_ = 0;
    GroupId group_ = 0;
public:
    using value_type = View;
    using reference = View;
    using pointer = void;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    constexpr IndexedViewIterator() noexcept = default;
    constexpr IndexedViewIterator(const Descriptor* rows, std::size_t position,
                                  GroupId group) noexcept
        : rows_(rows), position_(position), group_(group) {}

    // As with ordinary iterators, dereference requires a position before end.
    // Only that valid element position is narrowed to its packed-ID component.
    constexpr View operator*() const noexcept
    { return View{group_, static_cast<EntryOffset>(position_), rows_[position_]}; }

    struct ArrowProxy {
        View view;
        constexpr const View* operator->() const noexcept { return std::addressof(view); }
    };
    constexpr ArrowProxy operator->() const noexcept { return {**this}; }
    constexpr IndexedViewIterator& operator++() noexcept { ++position_; return *this; }
    constexpr IndexedViewIterator operator++(int) noexcept
    { const auto previous = *this; ++*this; return previous; }
    friend constexpr bool operator==(IndexedViewIterator left, IndexedViewIterator right) noexcept
    { return left.rows_ == right.rows_ && left.position_ == right.position_ && left.group_ == right.group_; }
    friend constexpr bool operator!=(IndexedViewIterator left, IndexedViewIterator right) noexcept
    { return !(left == right); }
};

template <class View>
class IndexedViewRange {
    using Descriptor = typename View::Descriptor;
    const Descriptor* rows_ = nullptr;
    std::size_t count_ = 0;
    GroupId group_ = 0;
public:
    using iterator = IndexedViewIterator<View>;
    constexpr IndexedViewRange() noexcept = default;
    // The supplied extent must belong to a live array. Like runtime indexes,
    // null storage is empty and larger extents are capped to the ID capacity.
    constexpr IndexedViewRange(const Descriptor* rows, std::size_t count, GroupId group = 0) noexcept
        : rows_(rows), count_(rows == nullptr ? 0
              : (count < idComponentCapacity ? count : idComponentCapacity)), group_(group) {}

    constexpr iterator begin() const noexcept { return {rows_, 0, group_}; }
    constexpr iterator end() const noexcept { return {rows_, count_, group_}; }
    constexpr std::size_t size() const noexcept { return count_; }
    constexpr bool empty() const noexcept { return count_ == 0; }
};
} // namespace telemetry::detail
#endif
