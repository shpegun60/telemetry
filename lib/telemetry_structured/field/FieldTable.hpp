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
#include "../detail/Traversal.hpp"
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

    // Erased iteration borrows this table; typed traversal below borrows its
    // exact definitions. Neither interface stores or materializes a value.
    [[nodiscard]] constexpr bool empty() const noexcept { return staticSize == 0; }
    [[nodiscard]] constexpr const FieldEntry* begin() const& noexcept { return entries_.data(); }
    const FieldEntry* begin() const&& = delete;
    [[nodiscard]] constexpr const FieldEntry* end() const& noexcept
    {
        // std::array may expose checked iterators on other standard libraries.
        // Our view uses pointers and never adds zero to a possibly null data().
        if constexpr (staticSize == 0) return entries_.data();
        else return entries_.data() + staticSize;
    }
    const FieldEntry* end() const&& = delete;
    [[nodiscard]] constexpr const FieldEntry& operator[](std::size_t i) const& noexcept { return entries_[i]; }
    const FieldEntry& operator[](std::size_t) const&& = delete;

    template <auto Position>
    [[nodiscard]] constexpr decltype(auto) get() const& noexcept
    {
        constexpr auto i = telemetry::detail::positionValue<Position>();
        static_assert(i < staticSize, "Field position is outside this table");
        if constexpr (i < staticSize) return std::get<static_cast<std::size_t>(i)>(definitions_);
    }
    template <auto Position>
    void get() const&& = delete;

    template <class Visitor>
    constexpr void forEach(Visitor&& visitor) const&
    {
        detail::forEachDefinition(definitions_, visitor, std::index_sequence_for<Definitions...>{});
    }
    template <class Visitor>
    void forEach(Visitor&&) const&& = delete;

    // A local position may be an integer or scoped position enum. Invalid
    // positions return false without invoking the visitor. Callback returns
    // are ignored; the bool reports selection, not the endpoint's status.
    template <class... Explicit, class Position, class Visitor>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
    [[nodiscard]] bool visit(Position position, Visitor&& visitor) const&
    {
        if (!telemetry::detail::indexFits<std::uint32_t>(position)) return false;
        const auto i = static_cast<std::uint32_t>(position);
        if (i >= staticSize) return false;
        using Dispatch = detail::DefinitionDispatch<std::tuple<Definitions...>,
                                                    std::remove_reference_t<Visitor>>;
        Dispatch::entries[i](definitions_, visitor);
        return true;
    }
    template <class... Explicit, class Position, class Visitor>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
    bool visit(Position, Visitor&&) const&& = delete;

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

    // Static forms retain the concrete target. Runtime forms specialize an
    // indexed dispatch table for To/From and never construct legacy Scalar.
    template <class To, auto Position>
        requires std::is_same_v<To, std::remove_cvref_t<To>>
    [[nodiscard]] std::optional<To> readAs() const& noexcept
    { return get<Position>().template readAs<To>(); }
    template <class To, auto Position>
    void readAs() const&& = delete;

    template <auto Position, class From>
        requires (!std::is_volatile_v<From>)
    [[nodiscard]] telemetry::WriteResult writeAs(const From& value) const& noexcept
    { return get<Position>().writeAs(value); }
    template <auto Position, class From>
    void writeAs(const From&) const&& = delete;

    template <class To, class... Explicit, class Position>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position> &&
                  std::is_same_v<To, std::remove_cvref_t<To>>)
    [[nodiscard]] std::optional<To> readAs(Position position) const& noexcept
    {
        static_assert(Type<To>::kind != TypeKind::Void, "Field readAs requires a native value type");
        if (!telemetry::detail::indexFits<std::uint32_t>(position)) return std::nullopt;
        const auto i = static_cast<std::uint32_t>(position);
        if (i >= staticSize) return std::nullopt;
        return detail::FieldAccessDispatch<std::tuple<Definitions...>, To>::reads[i](definitions_);
    }
    template <class To, class... Explicit, class Position>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
    void readAs(Position) const&& = delete;

    template <class... Explicit, class Position, class From>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position> &&
                  !std::is_volatile_v<From>)
    [[nodiscard]] telemetry::WriteResult writeAs(Position position, const From& value) const& noexcept
    {
        static_assert(Type<From>::kind != TypeKind::Void, "Field writeAs requires a native value type");
        if (!telemetry::detail::indexFits<std::uint32_t>(position)) return telemetry::WriteResult::NotFound;
        const auto i = static_cast<std::uint32_t>(position);
        if (i >= staticSize) return telemetry::WriteResult::NotFound;
        return detail::FieldAccessDispatch<std::tuple<Definitions...>, From>::writes[i](definitions_, value);
    }
    template <class... Explicit, class Position, class From>
        requires (sizeof...(Explicit) == 0 && telemetry::detail::isIdInput<Position>)
    void writeAs(Position, const From&) const&& = delete;

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
