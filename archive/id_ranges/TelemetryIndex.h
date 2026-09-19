#ifndef TELEMETRY_INDEX_H
#define TELEMETRY_INDEX_H

#include <array>
#include <limits>

#include "TelemetryCatalog.h"

namespace telemetry {

enum class IndexStatus {
    Ok,
    InvalidInput,
    IdOutOfRange,
    DuplicateId,
};

// Direct addressing for arbitrary row order and gaps. One pointer per ID
// in [FirstId, FirstId + Span). No allocation, sorting or lookup loop.
// Arrays, their IDs/order/addresses, and bound source objects must remain
// valid and unchanged for the index lifetime; source values may change.
template <FieldId FirstId, std::size_t Span>
class FieldIndex {
    static_assert(Span == 0 || Span - 1 <= std::numeric_limits<FieldId>::max() - FirstId,
                  "The index ID range must not wrap around FieldId");

public:
    constexpr FieldIndex() noexcept = default;

    constexpr FieldIndex(const Field* fields, std::size_t count) noexcept
    {
        append_(fields, count);
    }

    constexpr FieldIndex(const Catalog* catalogs, std::size_t count) noexcept
    {
        if (catalogs == nullptr && count != 0) {
            fail_(IndexStatus::InvalidInput);
            return;
        }
        for (std::size_t c = 0; c < count && valid(); ++c) {
            append_(catalogs[c].fields, catalogs[c].count);
        }
    }

    constexpr const Field* find(FieldId id) const noexcept
    {
        const FieldId offset = static_cast<FieldId>(id - FirstId);
        return offset < Span ? slots_[offset] : nullptr;
    }

    constexpr bool valid() const noexcept { return status_ == IndexStatus::Ok; }
    constexpr IndexStatus status() const noexcept { return status_; }
    constexpr std::size_t size() const noexcept { return size_; }

private:
    constexpr void fail_(IndexStatus status) noexcept
    {
        // Failure never exposes a partially built index. Clearing once here
        // keeps validity checks out of the hot lookup path.
        for (std::size_t i = 0; i < Span; ++i) slots_[i] = nullptr;
        size_ = 0;
        status_ = status;
    }

    constexpr void append_(const Field* fields, std::size_t count) noexcept
    {
        if (fields == nullptr && count != 0) {
            fail_(IndexStatus::InvalidInput);
            return;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const FieldId offset = static_cast<FieldId>(fields[i].id - FirstId);
            if (offset >= Span) {
                fail_(IndexStatus::IdOutOfRange);
                return;
            }
            if (slots_[offset] != nullptr) {
                fail_(IndexStatus::DuplicateId);
                return;
            }
            slots_[offset] = &fields[i];
            ++size_;
        }
    }

    std::array<const Field*, Span> slots_{};
    std::size_t size_ = 0;
    IndexStatus status_ = IndexStatus::Ok;
};

// The smallest lookup for a table whose explicit IDs equal FirstId + row.
// Retains only the original array's address. Construction validates every
// row once; a failed build remains empty. For constexpr tables use
// static_assert(range.valid()) to make a misplaced ID a compile error.
template <FieldId FirstId, std::size_t Count>
class FieldRange {
    static_assert(Count > 0, "A positional field range needs a nonempty array");
    static_assert(Count == 0 || Count - 1 <= std::numeric_limits<FieldId>::max() - FirstId,
                  "The field ID range must not wrap around FieldId");

public:
    FieldRange(const Field (&&)[Count]) = delete;

    constexpr explicit FieldRange(const Field (&fields)[Count]) noexcept
    {
        for (std::size_t i = 0; i < Count; ++i) {
            if (fields[i].id != FirstId + static_cast<FieldId>(i)) return;
        }
        fields_ = fields;
    }

    constexpr const Field* find(FieldId id) const noexcept
    {
        const FieldId offset = static_cast<FieldId>(id - FirstId);
        return fields_ != nullptr && offset < Count ? fields_ + offset : nullptr;
    }

    constexpr bool valid() const noexcept { return fields_ != nullptr; }

private:
    const Field* fields_ = nullptr;
};

template <FieldId FirstId, std::size_t Count>
constexpr FieldRange<FirstId, Count> make_field_range(const Field (&fields)[Count]) noexcept
{
    return FieldRange<FirstId, Count>(fields);
}

template <FieldId FirstId, std::size_t Count>
FieldRange<FirstId, Count> make_field_range(const Field (&&)[Count]) = delete;

} // namespace telemetry

#endif
