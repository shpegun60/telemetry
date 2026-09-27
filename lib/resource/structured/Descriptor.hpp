/*
 * @file Descriptor.hpp
 * @brief Immutable structural descriptor with constexpr hash and indexed resume.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef RESOURCE_STRUCTURED_DESCRIPTOR_HPP
#define RESOURCE_STRUCTURED_DESCRIPTOR_HPP

#include "../Types.hpp"
#include "detail/Segments.hpp"
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace resource::structured {
namespace ts = telemetry::structured;

template <class Fields, class Commands, class Services, class Profile>
class Descriptor;

enum class DescriptorError : std::uint8_t {
    None, InvalidName, DuplicateName, TooLarge, MetadataMismatch
};

namespace detail {

template <class... Groups>
struct CatalogShape {
    static constexpr std::size_t catalogs = sizeof...(Groups);
    static constexpr std::size_t endpoints = (std::size_t{0} + ... + Groups::TableType::staticSize);
};
template <class Catalogs> struct Shape;
template <> struct Shape<ts::EmptyEndpointCatalog> : CatalogShape<> {};
template <class... G> struct Shape<ts::FieldCatalogTable<G...>> : CatalogShape<G...> {};
template <class... G> struct Shape<ts::CommandCatalogTable<G...>> : CatalogShape<G...> {};
template <class... G> struct Shape<ts::ServiceCatalogTable<G...>> : CatalogShape<G...> {};

// Writable is a declaration capability, not a test of a slot/function address.
// No extra member is added to hot FieldEntry or ModelView for serialization.
template <class Table> struct FieldCapabilities;
template <class... Definitions>
struct FieldCapabilities<ts::FieldTable<Definitions...>> {
    inline static constexpr std::array<bool, sizeof...(Definitions)> values{Definitions::writable...};
};
template <class Catalogs> struct Writable;
template <class... Groups>
struct Writable<ts::FieldCatalogTable<Groups...>> {
    inline static constexpr std::array<std::span<const bool>, sizeof...(Groups)> groups{
        std::span<const bool>{FieldCapabilities<typename Groups::TableType>::values}...};
    static constexpr bool at(std::uint32_t group, std::uint32_t entry) noexcept
    { return groups[group][entry]; }
};
template <> struct Writable<ts::EmptyEndpointCatalog> {
    static constexpr bool at(std::uint32_t, std::uint32_t) noexcept { return false; }
};

[[noreturn]] inline void invalidDescriptorName() noexcept { std::abort(); }
[[noreturn]] inline void duplicateDescriptorName() noexcept { std::abort(); }
[[noreturn]] inline void descriptorSizeExceeded() noexcept { std::abort(); }
[[noreturn]] inline void inconsistentDescriptorMetadata() noexcept { std::abort(); }

// In-place heapsort bounds construction work without a second table or heap.
// Segments are restored to positional order before offsets/hash are finalized.
template <class Less>
constexpr void sort(std::span<Segment> values, Less less) noexcept
{
    const auto sift = [&](std::size_t root, std::size_t count) constexpr {
        while (root < count / 2) {
            auto child = root * 2 + 1;
            if (child + 1 < count && less(values[child], values[child + 1])) ++child;
            if (!less(values[root], values[child])) return;
            const auto temp = values[root]; values[root] = values[child]; values[child] = temp;
            root = child;
        }
    };
    for (std::size_t start = values.size() / 2; start != 0; --start) sift(start - 1, values.size());
    for (std::size_t end = values.size(); end > 1; --end) {
        const auto temp = values[0]; values[0] = values[end - 1]; values[end - 1] = temp;
        sift(0, end - 1);
    }
}

} // namespace detail

// Borrowed view: the owning descriptor and its immutable Model metadata must
// outlive it. A cursor is a byte offset here, independent of resource framing.
class DescriptorView {
    template <class, class, class, class> friend class Descriptor;
    constexpr DescriptorView(const ts::ModelView& model, const detail::Header* header,
                             std::span<const detail::Segment> segments,
                             DescriptorError error) noexcept
        : model_(&model), header_(header), segments_(segments), error_(error) {}

public:
    [[nodiscard]] constexpr std::uint32_t size() const noexcept
    { return error_ == DescriptorError::None ? header_->totalBytes : 0; }
    [[nodiscard]] constexpr std::uint64_t fingerprint() const noexcept
    { return error_ == DescriptorError::None ? header_->fingerprint : 0; }
    [[nodiscard]] constexpr DescriptorError error() const noexcept { return error_; }

    [[nodiscard]] constexpr ReadResult read(Cursor cursor, Output output) const noexcept
    {
        if (error_ != DescriptorError::None) return {Status::InvalidData, cursor};
        if (cursor > size()) return {Status::InvalidCursor, cursor};
        if (cursor == size()) return {Status::Ok, cursor, 0, true};
        if (output.empty()) return {Status::BufferTooSmall, cursor};

        // Last entry is a sentinel at totalBytes. Binary search jumps directly
        // to a type/member/enum/name segment, including a cursor inside a string.
        std::size_t low = 0, high = segments_.size() - 1;
        while (low + 1 < high) {
            const auto middle = low + (high - low) / 2;
            if (segments_[middle].offset <= cursor) low = middle;
            else high = middle;
        }
        const auto remaining = size() - static_cast<std::uint32_t>(cursor);
        const auto count = output.size() < remaining ? output.size() : remaining;
        detail::SliceSink sink{output.first(count), static_cast<std::uint32_t>(cursor) - segments_[low].offset};
        while (sink.written < count) {
            detail::emit(segments_[low], segments_[low + 1].offset - segments_[low].offset,
                         *model_, *header_, sink);
            ++low;
        }
        const auto next = cursor + sink.written;
        return {Status::Ok, next, sink.written, next == size()};
    }

private:
    const ts::ModelView* model_;
    const detail::Header* header_;
    std::span<const detail::Segment> segments_;
    DescriptorError error_;
};

// The segment count depends only on structural types/table arities. Names and
// bindings can belong to a constexpr or runtime Model. No getter is invoked.
// The object owns offsets/header, and borrows the same metadata as ModelView.
template <class Fields, class Commands, class Services, class Profile = ts::Limits>
class Descriptor {
    using Model = ts::Model<Fields, Commands, Services>;
    using Registry = typename Model::Registry;
    using Kind = detail::SegmentKind;
    using Segment = detail::Segment;

    static constexpr std::size_t catalogCount = detail::Shape<Fields>::catalogs +
        detail::Shape<Commands>::catalogs + detail::Shape<Services>::catalogs;
    static constexpr std::size_t endpointCount = detail::Shape<Fields>::endpoints +
        detail::Shape<Commands>::endpoints + detail::Shape<Services>::endpoints;
    static_assert(catalogCount <= Profile::maxCatalogCountTotal, "Descriptor catalog ceiling exceeded");
    static_assert(endpointCount <= Profile::maxEndpointCountTotal, "Descriptor endpoint ceiling exceeded");
    static_assert(Registry::typeCount <= Profile::maxTypeCount, "Descriptor type ceiling exceeded");
    static_assert(Profile::maxDescriptorBytes <= UINT32_MAX, "Descriptor size ceiling exceeds u32");
    static_assert(Profile::maxStringBytes < UINT32_MAX, "Descriptor string ceiling exceeds u32");

    // The registry already proves structural validity. A descriptor profile
    // may tighten its resource ceilings; this check is compile-time only.
    static consteval bool typesFitProfile() noexcept
    {
        std::array<std::uint32_t, Registry::typeCount> depths{};
        std::array<std::uint32_t, Registry::typeCount> nodes{};
        std::uint64_t enumEntries = 0;
        for (const auto& type : std::span{Registry::view().data, Registry::typeCount}) {
            if (type.wireBytes > Profile::maxValueWireBytes ||
                type.memberCount > Profile::maxStructMembers ||
                type.elementCount > Profile::maxArrayElements) return false;
            enumEntries += type.enumCount;
            std::uint32_t depth = 0;
            std::uint64_t expanded = type.kind == ts::TypeKind::Void ? 0 : 1;
            if (type.kind == ts::TypeKind::Array) {
                depth = 1 + depths[type.relatedTypeId];
                expanded += std::uint64_t{type.elementCount} * nodes[type.relatedTypeId];
            } else if (type.kind == ts::TypeKind::Struct) {
                depth = 1;
                for (std::uint32_t i = 0; i < type.memberCount; ++i) {
                    const auto child = type.memberData[i].typeId;
                    if (depth <= depths[child]) depth = 1 + depths[child];
                    expanded += nodes[child];
                }
            }
            if (depth > Profile::maxTypeDepth || expanded > Profile::maxExpandedValueNodes)
                return false;
            depths[type.id] = depth;
            nodes[type.id] = static_cast<std::uint32_t>(expanded);
        }
        return enumEntries <= Profile::maxEnumEntriesTotal;
    }
    static_assert(typesFitProfile(), "Descriptor type resource ceiling exceeded");

    static consteval std::size_t countTypeSegments() noexcept
    {
        std::size_t result = Registry::typeCount;
        for (std::uint32_t i = 0; i < Registry::typeCount; ++i) {
            const auto& type = Registry::view().data[i];
            result += type.memberCount + type.enumCount;
        }
        return result;
    }
    static constexpr auto typeSegments = countTypeSegments();

public:
    static constexpr std::size_t segmentCount = 1 + typeSegments + catalogCount + endpointCount;
    static constexpr std::size_t indexBytes = (segmentCount + 1) * sizeof(Segment);

    constexpr explicit Descriptor(const Model& model) noexcept : model_(model.view())
    {
        build();
    }

    [[nodiscard]] constexpr DescriptorError error() const noexcept { return error_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return error_ == DescriptorError::None; }
    [[nodiscard]] constexpr std::uint32_t size() const noexcept { return valid() ? header_.totalBytes : 0; }
    [[nodiscard]] constexpr std::uint64_t fingerprint() const noexcept { return valid() ? header_.fingerprint : 0; }
    [[nodiscard]] constexpr DescriptorView view() const& noexcept
    { return {model_, &header_, segments_, error_}; }
    DescriptorView view() const&& = delete;
    [[nodiscard]] constexpr ReadResult read(Cursor cursor, Output output) const noexcept
    { return view().read(cursor, output); }

private:
    constexpr void fail(DescriptorError error) noexcept
    {
        error_ = error;
        if (!std::is_constant_evaluated()) return;
        switch (error) {
        case DescriptorError::InvalidName: detail::invalidDescriptorName();
        case DescriptorError::DuplicateName: detail::duplicateDescriptorName();
        case DescriptorError::TooLarge: detail::descriptorSizeExceeded();
        default: detail::inconsistentDescriptorMetadata();
        }
    }

    constexpr std::uint32_t nameBytes(std::string_view name) noexcept
    {
        if (name.size() > Profile::maxStringBytes ||
            !ts::reflection::detail::validUtf8(name)) {
            fail(DescriptorError::InvalidName);
            return 0;
        }
        return static_cast<std::uint32_t>(name.size());
    }
    constexpr std::uint32_t nameBytes(const char* text) noexcept
    {
        if (text == nullptr) { fail(DescriptorError::InvalidName); return 0; }
        std::uint32_t size = 0;
        while (size <= Profile::maxStringBytes && text[size] != '\0') ++size;
        if (size > Profile::maxStringBytes) { fail(DescriptorError::InvalidName); return 0; }
        return nameBytes(std::string_view{text, size});
    }

    constexpr bool add(std::size_t& next, std::uint64_t bytes, Kind kind,
                       std::uint32_t key = 0, std::uint32_t index = 0, bool writable = false) noexcept
    {
        if (!valid()) return false;
        if (next >= segmentCount || index > (UINT32_MAX >> 5)) {
            fail(DescriptorError::MetadataMismatch); return false;
        }
        if (bytes > Profile::maxDescriptorBytes || header_.totalBytes > Profile::maxDescriptorBytes - bytes) {
            fail(DescriptorError::TooLarge); return false;
        }
        header_.totalBytes += static_cast<std::uint32_t>(bytes);
        segments_[next++] = {static_cast<std::uint32_t>(bytes), key,
            static_cast<std::uint32_t>(kind) | (index << 5) | (writable ? 16u : 0u)};
        return true;
    }

    constexpr bool unique(std::size_t begin, std::size_t count, bool endpoints) noexcept
    {
        auto entries = std::span{segments_}.subspan(begin, count);
        const auto name = [&](const Segment& part) constexpr {
            return detail::segmentName(part, part.offset, model_);
        };
        detail::sort(entries, [&](const Segment& a, const Segment& b) constexpr {
            if (endpoints && (a.key >> 16) != (b.key >> 16)) return (a.key >> 16) < (b.key >> 16);
            return name(a) < name(b);
        });
        for (std::size_t i = 1; i < count; ++i) {
            if ((!endpoints || (entries[i - 1].key >> 16) == (entries[i].key >> 16)) &&
                name(entries[i - 1]) == name(entries[i])) {
                fail(DescriptorError::DuplicateName); return false;
            }
        }
        detail::sort(entries, [](const Segment& a, const Segment& b) constexpr { return a.key < b.key; });
        return true;
    }

    template <class Index>
    constexpr bool catalogs(std::size_t& next, Index index, Kind kind, unsigned category) noexcept
    {
        const auto begin = next;
        std::uint32_t ordinal = 0;
        for (std::uint32_t group = 0; group < index.count(); ++group) {
            const auto& catalog = index.catalogs()[group];
            if (!add(next, 28ULL + nameBytes(catalog.name), kind, group, ordinal)) return false;
            ordinal += catalog.count; // Compile-time table arities already bound the sum.
        }
        if (ordinal != header_.endpointCount[category] || index.count() != header_.catalogCount[category]) {
            fail(DescriptorError::MetadataMismatch); return false;
        }
        return unique(begin, next - begin, false);
    }

    template <class Index>
    constexpr bool endpoints(std::size_t& next, Index index, Kind kind) noexcept
    {
        const auto begin = next;
        for (std::uint32_t group = 0; group < index.count(); ++group) {
            const auto& catalog = index.catalogs()[group];
            for (std::uint32_t entry = 0; entry < catalog.count; ++entry) {
                const auto bytes = (kind == Kind::Command ? 20ULL : 24ULL) + nameBytes(catalog.entries[entry].name);
                const bool writable = kind == Kind::Field && detail::Writable<Fields>::at(group, entry);
                if (!add(next, bytes, kind, (group << 16) | entry, 0, writable)) return false;
            }
        }
        return unique(begin, next - begin, true);
    }

    constexpr void build() noexcept
    {
        header_.typeCount = Registry::typeCount;
        header_.catalogCount = {detail::Shape<Fields>::catalogs, detail::Shape<Commands>::catalogs, detail::Shape<Services>::catalogs};
        header_.endpointCount = {detail::Shape<Fields>::endpoints, detail::Shape<Commands>::endpoints, detail::Shape<Services>::endpoints};
        std::size_t next = 0;
        if (!add(next, descriptorHeaderBytes, Kind::Header)) return;
        for (std::uint32_t i = 0; i < header_.typeCount; ++i) {
            const auto& type = model_.types.data[i];
            const auto start = header_.totalBytes;
            const auto bytes = type.kind == ts::TypeKind::Void ? 20 :
                type.kind == ts::TypeKind::Scalar || type.kind == ts::TypeKind::Struct ? 24 : 28;
            if (!add(next, bytes, Kind::Type, i)) return;
            for (std::uint32_t m = 0; m < type.memberCount; ++m)
                if (!add(next, 8ULL + nameBytes(type.memberData[m].name), Kind::Member, i, m)) return;
            for (std::uint32_t e = 0; e < type.enumCount; ++e)
                if (!add(next, type.wireBytes + 4ULL + nameBytes(type.enumData[e].name), Kind::EnumEntry, i, e)) return;
            if (header_.totalBytes - start != type.recordBytes) {
                fail(DescriptorError::MetadataMismatch); return;
            }
        }
        if (header_.totalBytes != descriptorHeaderBytes + model_.types.recordsBytes) {
            fail(DescriptorError::MetadataMismatch); return;
        }
        header_.catalogsOffset = header_.totalBytes;
        if (!catalogs(next, model_.fields, Kind::FieldCatalog, 0) ||
            !catalogs(next, model_.commands, Kind::CommandCatalog, 1) ||
            !catalogs(next, model_.services, Kind::ServiceCatalog, 2)) return;
        header_.endpointsOffset = header_.totalBytes;
        if (!endpoints(next, model_.fields, Kind::Field) ||
            !endpoints(next, model_.commands, Kind::Command) ||
            !endpoints(next, model_.services, Kind::Service)) return;
        if (next != segmentCount) { fail(DescriptorError::MetadataMismatch); return; }

        std::uint32_t offset = 0;
        for (std::size_t i = 0; i < segmentCount; ++i) {
            const auto bytes = segments_[i].offset;
            segments_[i].offset = offset;
            offset += bytes; // add() checked the total before storing lengths.
        }
        segments_[segmentCount] = {offset, 0, 0};
        detail::HashSink hash;
        for (std::size_t i = 0; i < segmentCount; ++i)
            detail::emit(segments_[i], segments_[i + 1].offset - segments_[i].offset, model_, header_, hash);
        header_.fingerprint = hash.value; // Header hash bytes were zero during emission.
    }

    ts::ModelView model_;
    detail::Header header_{};
    std::array<Segment, segmentCount + 1> segments_{};
    DescriptorError error_ = DescriptorError::None;
};

template <class F, class C, class S>
Descriptor(const ts::Model<F, C, S>&) -> Descriptor<F, C, S>;

// Optional Flash-byte representation for comparison or a small immutable model.
// It is not also stored by the streaming Descriptor, and never allocates RAM.
template <const auto& Source>
[[nodiscard]] consteval auto packDescriptor() noexcept
{
    static_assert(Source.valid(), "Cannot pack an invalid descriptor");
    std::array<std::byte, Source.size()> result{};
    const auto read = Source.read(0, result);
    if (read.status != Status::Ok || !read.eof || read.written != result.size())
        detail::inconsistentDescriptorMetadata();
    return result;
}

} // namespace resource::structured
#endif
