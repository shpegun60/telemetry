/*
 * @file RegistryProbe.cpp
 * @brief Stage 05 type order, metadata, size and runtime view checks.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#include "ProbeTypes.hpp"

#include <telemetry_structured/abi/StructuredAbi.hpp>
#include <telemetry_structured/type/Registry.hpp>

#include <array>
#include <cstdint>
#include <type_traits>

using telemetry::structured::TypeKind;
using telemetry::structured::TypeRegistry;
using registry_probe::Mode;
using registry_probe::Reading;
using registry_probe::SameShape;
using registry_probe::SampleBlock;

struct EmptyStruct {};
enum class NoNames : std::uint8_t { One = 1 };

template <>
struct telemetry::structured::reflection::EnumReflection<NoNames> {
    inline static constexpr auto entries =
        telemetry::structured::reflection::enumEntries<NoNames>();
};

using Empty = TypeRegistry<>;
using Registry = TypeRegistry<SampleBlock, Reading, SameShape, const Reading&, Mode>;
using Reordered = TypeRegistry<SameShape, Reading>;
using EmptyShapes = TypeRegistry<EmptyStruct, std::array<std::uint16_t, 0>, NoNames>;
using ReadingAlias = Reading;
using Repeated = TypeRegistry<
    Reading, Reading, Reading, Reading, Reading,
    Reading, Reading, Reading, Reading, Reading,
    Reading, Reading, Reading, Reading, Reading,
    Reading, Reading, Reading, Reading, Reading>;

static_assert(Empty::typeCount == 12);
static_assert(Empty::typeId<void>() == 0);
static_assert(Empty::typeId<bool>() == 1);
static_assert(Empty::typeId<std::uint8_t>() == 2);
static_assert(Empty::typeId<std::int8_t>() == 3);
static_assert(Empty::typeId<std::uint16_t>() == 4);
static_assert(Empty::typeId<std::int16_t>() == 5);
static_assert(Empty::typeId<std::uint32_t>() == 6);
static_assert(Empty::typeId<std::int32_t>() == 7);
static_assert(Empty::typeId<std::uint64_t>() == 8);
static_assert(Empty::typeId<std::int64_t>() == 9);
static_assert(Empty::typeId<float>() == 10);
static_assert(Empty::typeId<double>() == 11);
static_assert(Empty::recordsBytes == 20 + 11 * 24);

// A nested member is registered before its parent; later roots are deduped.
static_assert(Registry::typeId<Reading>() == 12);
static_assert(Registry::typeId<std::array<std::uint16_t, 3>>() == 13);
static_assert(Registry::typeId<Mode>() == 14);
static_assert(Registry::typeId<SampleBlock>() == 15);
static_assert(Registry::typeId<SameShape>() == 16);
static_assert(Registry::typeCount == 17);
static_assert(Registry::typeId<const Reading&>() == Registry::typeId<Reading>());
static_assert(Registry::typeId<ReadingAlias>() == Registry::typeId<Reading>());
static_assert(Registry::typeId<SameShape>() != Registry::typeId<Reading>());
static_assert(Reordered::typeId<SameShape>() == 12);
static_assert(Reordered::typeId<Reading>() == 13);
static_assert(Repeated::typeCount == 13);
static_assert(EmptyShapes::typeCount == 15);
static_assert(EmptyShapes::descriptor<12>().kind == TypeKind::Struct);
static_assert(EmptyShapes::descriptor<12>().memberCount == 0);
static_assert(EmptyShapes::descriptor<12>().memberData == nullptr);
static_assert(EmptyShapes::descriptor<12>().recordBytes == 24);
static_assert(EmptyShapes::descriptor<13>().kind == TypeKind::Array);
static_assert(EmptyShapes::descriptor<13>().wireBytes == 0);
static_assert(EmptyShapes::descriptor<14>().kind == TypeKind::Enum);
static_assert(EmptyShapes::descriptor<14>().enumCount == 0);
static_assert(EmptyShapes::descriptor<14>().enumData == nullptr);
static_assert(EmptyShapes::descriptor<14>().recordBytes == 28);

static_assert(Registry::descriptor<13>().kind == TypeKind::Array);
static_assert(Registry::descriptor<13>().relatedTypeId == 4);
static_assert(Registry::descriptor<13>().elementCount == 3);
static_assert(Registry::descriptor<14>().kind == TypeKind::Enum);
static_assert(Registry::descriptor<14>().relatedTypeId == 5);
static_assert(Registry::descriptor<14>().enumCount == 3);
static_assert(Registry::descriptor<14>().enumEntry(0)->codeBits == 0xfffb);
static_assert(Registry::descriptor<14>().enumEntry(0)->name == "Below");
static_assert(Registry::descriptor<15>().kind == TypeKind::Struct);
static_assert(Registry::descriptor<15>().memberCount == 3);
static_assert(Registry::descriptor<15>().member(0)->typeId == 12);
static_assert(Registry::descriptor<15>().member(0)->name == "reading");
static_assert(Registry::descriptor<15>().member(1)->typeId == 13);
static_assert(Registry::descriptor<15>().member(2)->typeId == 14);
static_assert(Registry::descriptor<15>().wireBytes == 14);
static_assert(Registry::descriptor<13>().recordBytes == 28);
static_assert(Registry::descriptor<14>().recordBytes == 28 +
              (2 + 4 + 5) + (2 + 4 + 6) + (2 + 4 + 5));
static_assert(Registry::recordsBytes == 546);
static_assert(Registry::view().find(17) == nullptr);
static_assert(std::is_standard_layout_v<telemetry::structured::TypeDescriptor>);
static_assert(std::is_trivially_copyable_v<telemetry::structured::TypeDescriptor>);

template <class T>
concept HasSemanticMetadata =
    requires(const T& descriptor) { descriptor.unit; } ||
    requires(const T& descriptor) { descriptor.minimum; } ||
    requires(const T& descriptor) { descriptor.defaultValue; } ||
    requires(const T& descriptor) { descriptor.owner; };

static_assert(!HasSemanticMetadata<telemetry::structured::TypeDescriptor>);

extern "C" std::uint32_t registry_other_id() noexcept;

int main()
{
    telemetry::structured::requireStructuredAbi();
    auto view = Registry::view();
    if (view.count != Registry::typeCount || view.recordsBytes != Registry::recordsBytes)
        return 1;
    if (view.find(0) == nullptr || view.find(17) != nullptr)
        return 2;
    for (std::uint32_t i = 0; i < view.count; ++i) {
        const auto* descriptor = view.find(i);
        if (descriptor->id != i || descriptor->recordBytes < 20)
            return 3;
        if (descriptor->kind == TypeKind::Struct) {
            for (std::uint32_t member = 0; member < descriptor->memberCount; ++member)
                if (descriptor->member(member)->typeId >= i) return 4;
        } else if (descriptor->kind == TypeKind::Array || descriptor->kind == TypeKind::Enum) {
            if (descriptor->relatedTypeId >= i) return 5;
        }
    }
    return registry_other_id() == Registry::typeId<SampleBlock>() ? 0 : 6;
}
