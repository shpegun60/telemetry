/*
 * @file FieldTable.hpp
 * @brief One mixed native Field table with bounded encoded read/write.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_STRUCTURED_FIELD_FIELD_TABLE_HPP
#define TELEMETRY_STRUCTURED_FIELD_FIELD_TABLE_HPP

#include "Field.hpp"
#include "../result/EndpointResults.hpp"
#include "../detail/Encoded.hpp"
#include "../type/Registry.hpp"
#include <telemetry/core/TelemetryId.h>
#include <array>

namespace telemetry::structured {

struct FieldEntry {
    // Internal operations require the checked boundary below. Their typed
    // bodies already know the exact wire length; no runtime span is passed.
    using Read = EncodedReadResult (*)(const void*, std::byte*, Workspace&) noexcept;
    using Write = EncodedWriteResult (*)(const void*, const std::byte*, Workspace&) noexcept;
    const void* readContext;
    Read read;
    const void* writeContext;
    Write write; // nullptr denotes a read-only definition, not a temporarily empty slot.
    const char* name;
    std::uint32_t wireBytes;
    std::uint32_t scratchBytes;

    [[nodiscard]] EncodedReadResult readEncoded(std::span<std::byte> output,
                                                 Workspace& workspace) const noexcept
    {
        if (output.size() < wireBytes) return {DispatchStatus::BufferTooSmall, 0};
        if (scratchBytes != 0 && buffersOverlap(output, workspace.storage()))
            return {DispatchStatus::InvalidPayload, 0};
        return read(readContext, output.data(), workspace);
    }

    [[nodiscard]] EncodedWriteResult writeEncoded(std::span<const std::byte> input,
                                                   Workspace& workspace) const noexcept
    {
        if (write == nullptr) return {DispatchStatus::Ok, telemetry::WriteResult::ReadOnly};
        if (input.size() != wireBytes) return {DispatchStatus::InvalidPayload};
        if (scratchBytes != 0 && buffersOverlap(input, workspace.storage()))
            return {DispatchStatus::InvalidPayload};
        return write(writeContext, input.data(), workspace);
    }
};

template <class... Definitions>
class FieldTable {
    static_assert(sizeof...(Definitions) <= idComponentCapacity,
                  "Field table exceeds the 16-bit local position space");
public:
    using RootTypes = detail::TypeList<typename Definitions::Value...>;
    static constexpr std::size_t staticSize = sizeof...(Definitions);

    constexpr explicit FieldTable(Definitions... definitions) noexcept
        : definitions_(definitions...), entries_(makeEntries(std::index_sequence_for<Definitions...>{}))
    {}

    // Runtime-function/custom bindings can borrow definitions_ in this exact
    // table. Direct owner/slot contexts do not make all tables relocatable.
    FieldTable(const FieldTable&) = delete;
    FieldTable& operator=(const FieldTable&) = delete;
    FieldTable(FieldTable&&) = delete;
    FieldTable& operator=(FieldTable&&) = delete;

    [[nodiscard]] constexpr const FieldEntry* data() const& noexcept { return entries_.data(); }
    const FieldEntry* data() const&& = delete;
    [[nodiscard]] constexpr std::size_t size() const noexcept { return staticSize; }

    template <auto Position>
    [[nodiscard]] auto read() const noexcept
    {
        constexpr auto i = telemetry::detail::positionValue<Position>();
        static_assert(i < staticSize, "Field position is outside this table");
        if constexpr (i < staticSize) return std::get<static_cast<std::size_t>(i)>(definitions_).read();
    }

    template <auto Position, class Argument>
    [[nodiscard]] telemetry::WriteResult write(Argument&& value) const noexcept
    {
        constexpr auto i = telemetry::detail::positionValue<Position>();
        static_assert(i < staticSize, "Field position is outside this table");
        if constexpr (i < staticSize)
            return std::get<static_cast<std::size_t>(i)>(definitions_).write(std::forward<Argument>(value));
    }

    template <class Registry>
    struct TypeStorage {
        inline static constexpr std::array<TypeId, staticSize> entries{{
            Registry::template typeId<typename Definitions::Value>()...}};
    };

private:
    template <class Definition>
    static constexpr std::uint32_t requiredScratch = [] {
        if constexpr (detail::localObject<typename Definition::Value>) return std::uint32_t{0};
        static_assert(scratchBytes<typename Definition::Value> <= UINT32_MAX,
                      "Field scratch size overflows u32");
        return static_cast<std::uint32_t>(scratchBytes<typename Definition::Value>);
    }();

    template <class Definition>
    static EncodedReadResult readOne(const void* raw, std::byte* bytes,
                                      Workspace& workspace) noexcept
    {
        using Value = typename Definition::Value;
        using Getter = typename Definition::GetterBinding;
        const std::span<std::byte> output{bytes, wireSize<Value>};
        if constexpr (detail::localObject<Value>) {
            auto selected = detail::ErasedBinding<Getter>::snapshot(raw);
            if (!Getter::available(selected)) return {DispatchStatus::Unavailable, 0};
            const Value value = Getter::invoke(selected);
            detail::encodeEndpoint(value, output.first(wireSize<Value>));
            return {DispatchStatus::Ok, wireSize<Value>};
        } else {
            // The reservation itself checks remaining capacity and alignment.
            // The advertised scratchBytes includes worst-case alignment margin.
            auto lease = workspace.reserve<Value>();
            if (!lease.valid()) return {DispatchStatus::WorkspaceTooSmall, 0};
            auto selected = detail::ErasedBinding<Getter>::snapshot(raw);
            if (!Getter::available(selected)) return {DispatchStatus::Unavailable, 0};
            auto* value = lease.constructFrom([&]() -> Value { return Getter::invoke(selected); });
            detail::encodeEndpoint(*value, output.first(wireSize<Value>));
            return {DispatchStatus::Ok, wireSize<Value>};
        }
    }

    template <class Definition>
    static EncodedWriteResult writeReady(const void* context,
                                         const typename Definition::Value& value) noexcept
    {
        using Setter = typename Definition::SetterBinding;
        auto selected = detail::ErasedBinding<Setter>::snapshot(context);
        if (!Setter::available(selected)) return {DispatchStatus::Unavailable};
        const auto result = Setter::invoke(selected, value);
        if (!model_detail::validStatus(result)) return {DispatchStatus::InternalError};
        return {DispatchStatus::Ok, result};
    }

    template <class Definition>
    static EncodedWriteResult writeOne(const void* raw, const std::byte* bytes,
                                        Workspace& workspace) noexcept
    {
        using Value = typename Definition::Value;
        const std::span<const std::byte> input{bytes, wireSize<Value>};
        if constexpr (detail::localObject<Value>) {
            if (!detail::validEndpoint<Value>(input)) return {DispatchStatus::InvalidPayload};
            const Value value = detail::decodeLocalEndpoint<Value>(input);
            return writeReady<Definition>(raw, value);
        } else {
            auto lease = workspace.reserve<Value>();
            if (!lease.valid()) return {DispatchStatus::WorkspaceTooSmall};
            Value* value = detail::decodeEndpoint<Value>(input, lease);
            if (value == nullptr) return {DispatchStatus::InvalidPayload};
            return writeReady<Definition>(raw, *value);
        }
    }

    template <class Definition>
    static consteval FieldEntry::Write writer() noexcept
    {
        if constexpr (Definition::writable) return &writeOne<Definition>;
        else return nullptr;
    }

    template <class Definition>
    static constexpr const void* writeContext(const Definition& definition) noexcept
    {
        if constexpr (Definition::writable)
            return detail::ErasedBinding<typename Definition::SetterBinding>::context(definition.setter_);
        else
            return nullptr;
    }

    template <std::size_t... I>
    constexpr auto makeEntries(std::index_sequence<I...>) noexcept
    {
        return std::array<FieldEntry, staticSize>{{FieldEntry{
            detail::ErasedBinding<typename Definitions::GetterBinding>::context(std::get<I>(definitions_).getter_),
            &readOne<Definitions>, writeContext(std::get<I>(definitions_)), writer<Definitions>(),
            std::get<I>(definitions_).name(), wireSize<typename Definitions::Value>,
            requiredScratch<Definitions>}...}};
    }

    std::tuple<Definitions...> definitions_;
    std::array<FieldEntry, staticSize> entries_;
};

template <class... Definitions>
FieldTable(Definitions...) -> FieldTable<Definitions...>;

} // namespace telemetry::structured

#endif
