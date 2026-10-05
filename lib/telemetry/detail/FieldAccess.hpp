/*
 * @file FieldAccess.hpp
 * @brief Native field access with explicit checked conversion, without Scalar.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

/*
 * Implements the explicit native readAs/writeAs Field conversion interface.
 *
 * Exact structural types pass directly; numeric types use checked conversion.
 * Incompatible structures and failed conversions stop before application
 * mutation. Runtime dispatch specializes homogeneous result tables for the
 * requested type, avoiding a universal value container or visitor-owned copy.
 */

#ifndef TELEMETRY_DETAIL_FIELD_ACCESS_HPP
#define TELEMETRY_DETAIL_FIELD_ACCESS_HPP
#pragma once

#include <telemetry/detail/NumberConversion.hpp>
#include <telemetry/result/EndpointStatus.hpp>
#include <telemetry/result/BorrowedValue.hpp>
#include <telemetry/result/FieldReadResult.hpp>
#include <array>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::detail {

template<class T>
inline constexpr bool nativeNumber =
    (std::is_integral_v<T> && sizeof(T) <= sizeof(std::uint64_t)) || std::is_same_v<T, float> ||
    std::is_same_v<T, double> || std::is_enum_v<T>;

// Selects a numeric type or an enum underlying representation.
template<class T, bool = std::is_enum_v<T>>
struct NumberRepresentation {
	using Type = T;
};

// Selects a numeric type or an enum underlying representation.
template<class T>
struct NumberRepresentation<T, true> {
	using Type = std::underlying_type_t<T>;
};

// Enums convert through their exact underlying integer. Representability is
// checked independently of dictionary membership, matching native wire values.
template<class To, class From>
[[nodiscard]] constexpr std::optional<To> checkedFieldNumber(From value) noexcept
{
	using Destination = typename NumberRepresentation<To>::Type;
	using Source = typename NumberRepresentation<From>::Type;
	Destination converted;
	if (!telemetry::detail::convertNumberTo(static_cast<Source>(value), converted))
		return std::nullopt;
	// Unknown enum codes are allowed when representable in the underlying
	// type, just as they are in the canonical codec. No dictionary filtering.
	return static_cast<To>(converted);
}

template<class To, class Definition>
[[nodiscard]] TELEMETRY_FORCE_INLINE std::optional<To>
readFieldAs(const Definition& definition) noexcept
{
	using From = typename Definition::Value;
	if constexpr (std::is_same_v<To, From>) {
		// Return directly: explicitly asking for a large native T does not
		// create an intermediate universal value container or another copy.
		if constexpr (Definition::borrowsValue) {
			// readAs explicitly asks for an owning value, even when read()
			// exposes a view. Construct directly from the referenced object.
			const auto value = definition.read();
			if (!value)
				return std::nullopt;
			return std::optional<To>{*value};
		} else {
			return definition.read();
		}
	} else if constexpr (nativeNumber<To> && nativeNumber<From>) {
		const auto value = definition.read();
		if (!value)
			return std::nullopt;
		return checkedFieldNumber<To>(*value);
	} else {
		// An incompatible structural type does not cause a getter call.
		return std::nullopt;
	}
}

template<class Definition, class From>
[[nodiscard]] telemetry::WriteResult writeFieldAs(const Definition& definition,
                                                  const From& value) noexcept
{
	using To = typename Definition::Value;
	if constexpr (!Definition::writable) {
		return telemetry::WriteResult::ReadOnly;
	} else if constexpr (std::is_same_v<To, From>) {
		return definition.write(value);
	} else if constexpr (nativeNumber<To> && nativeNumber<From>) {
		const auto converted = checkedFieldNumber<To>(value);
		return converted ? definition.write(*converted) : telemetry::WriteResult::InvalidValue;
	} else {
		return telemetry::WriteResult::InvalidValue;
	}
}

// Shape preflight precedes availability checks and getter invocation. An
// exact borrowed declaration returns its existing view without any T storage.
template<class To, class Definition>
[[nodiscard]] BorrowedValue<To> readFieldBorrowed(const Definition& definition) noexcept
{
	if constexpr (std::is_same_v<To, typename Definition::Value> && Definition::borrowsValue)
		return definition.read();
	else
		return {};
}

template<class To, class Definition>
[[nodiscard]] FieldReadResult<To> readFieldAsResult(const Definition& definition) noexcept
{
	using From = typename Definition::Value;
	if constexpr (std::is_same_v<To, From> || (nativeNumber<To> && nativeNumber<From>))
		return definition.template readAsResult<To>();
	else
		return FieldReadResult<To>::failure(FieldReadStatus::TypeMismatch);
}

// Each requested native type gets homogeneous borrowed and owning-result
// branches. Lookup storage contains only function pointers, never a value T.
// Public methods:
// - borrow(): Borrow selected exact declaration.
// - read(): Read selected declaration with failure status.
// - makeBorrows(): Build borrowed readers.
// - makeReads(): Build result readers.
template<class Tuple, class Value>
struct FieldReadDispatch {
	using Borrow = BorrowedValue<Value> (*)(const Tuple&) noexcept;
	using Read = FieldReadResult<Value> (*)(const Tuple&) noexcept;

	template<std::size_t I>
	static BorrowedValue<Value> borrow(const Tuple& definitions) noexcept
	{
		return readFieldBorrowed<Value>(std::get<I>(definitions));
	}

	template<std::size_t I>
	static FieldReadResult<Value> read(const Tuple& definitions) noexcept
	{
		return readFieldAsResult<Value>(std::get<I>(definitions));
	}

	template<std::size_t... I>
	static consteval auto makeBorrows(std::index_sequence<I...>) noexcept
	{
		return std::array<Borrow, sizeof...(I)>{{&borrow<I>...}};
	}

	template<std::size_t... I>
	static consteval auto makeReads(std::index_sequence<I...>) noexcept
	{
		return std::array<Read, sizeof...(I)>{{&read<I>...}};
	}

	inline static constexpr auto borrows =
	    makeBorrows(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
	inline static constexpr auto reads =
	    makeReads(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

// Group selection reuses each local table's per-type checked dispatch. Packed
// positions are already validated by the catalog before these branches run.
// Public methods:
// - borrow(): Borrow selected group's entry.
// - read(): Read selected group's entry with failure status.
// - makeBorrows(): Build group borrowers.
// - makeReads(): Build group readers.
template<class Tuple, class Value>
struct FieldGroupReadDispatch {
	using Borrow = BorrowedValue<Value> (*)(const Tuple&, std::uint32_t) noexcept;
	using Read = FieldReadResult<Value> (*)(const Tuple&, std::uint32_t) noexcept;

	template<std::size_t Group>
	static BorrowedValue<Value> borrow(const Tuple& groups, std::uint32_t entry) noexcept
	{
		return std::get<Group>(groups).table->template readBorrowed<Value>(entry);
	}

	template<std::size_t Group>
	static FieldReadResult<Value> read(const Tuple& groups, std::uint32_t entry) noexcept
	{
		return std::get<Group>(groups).table->template readAsResult<Value>(entry);
	}

	template<std::size_t... Group>
	static consteval auto makeBorrows(std::index_sequence<Group...>) noexcept
	{
		return std::array<Borrow, sizeof...(Group)>{{&borrow<Group>...}};
	}

	template<std::size_t... Group>
	static consteval auto makeReads(std::index_sequence<Group...>) noexcept
	{
		return std::array<Read, sizeof...(Group)>{{&read<Group>...}};
	}

	inline static constexpr auto borrows =
	    makeBorrows(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
	inline static constexpr auto reads =
	    makeReads(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

// Homogeneous result types let runtime readAs/writeAs return directly from the
// selected branch. This avoids a visitor's external optional<T> plus temporary
// when the caller explicitly requests a large exact native T.
// Public methods:
// - read(): Read selected definition.
// - write(): Apply selected definition.
// - makeReads(): Build read branches.
// - makeWrites(): Build write branches.
template<class Tuple, class Value>
struct FieldAccessDispatch {
	using Read = std::optional<Value> (*)(const Tuple&) noexcept;
	using Write = telemetry::WriteResult (*)(const Tuple&, const Value&) noexcept;

	template<std::size_t I>
	static std::optional<Value> read(const Tuple& definitions) noexcept
	{
		return readFieldAs<Value>(std::get<I>(definitions));
	}

	template<std::size_t I>
	static telemetry::WriteResult write(const Tuple& definitions, const Value& value) noexcept
	{
		return writeFieldAs(std::get<I>(definitions), value);
	}

	template<std::size_t... I>
	static consteval auto makeReads(std::index_sequence<I...>) noexcept
	{
		return std::array<Read, sizeof...(I)>{{&read<I>...}};
	}

	template<std::size_t... I>
	static consteval auto makeWrites(std::index_sequence<I...>) noexcept
	{
		return std::array<Write, sizeof...(I)>{{&write<I>...}};
	}

	inline static constexpr auto reads =
	    makeReads(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
	inline static constexpr auto writes =
	    makeWrites(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

// Select a group first, then let its local table validate the entry. Each
// generated branch preserves the same requested owning result type, allowing
// direct returns without a flattened runtime value or visitor accumulator.
// Public methods:
// - read(): Read selected group.
// - write(): Apply selected group.
// - makeReads(): Build group readers.
// - makeWrites(): Build group writers.
template<class Tuple, class Value>
struct FieldGroupAccessDispatch {
	using Read = std::optional<Value> (*)(const Tuple&, std::uint32_t) noexcept;
	using Write = telemetry::WriteResult (*)(const Tuple&, std::uint32_t, const Value&) noexcept;

	template<std::size_t Group>
	static std::optional<Value> read(const Tuple& groups, std::uint32_t entry) noexcept
	{
		return std::get<Group>(groups).table->template readAs<Value>(entry);
	}

	template<std::size_t Group>
	static telemetry::WriteResult write(const Tuple& groups, std::uint32_t entry,
	                                    const Value& value) noexcept
	{
		return std::get<Group>(groups).table->writeAs(entry, value);
	}

	template<std::size_t... Group>
	static consteval auto makeReads(std::index_sequence<Group...>) noexcept
	{
		return std::array<Read, sizeof...(Group)>{{&read<Group>...}};
	}

	template<std::size_t... Group>
	static consteval auto makeWrites(std::index_sequence<Group...>) noexcept
	{
		return std::array<Write, sizeof...(Group)>{{&write<Group>...}};
	}

	inline static constexpr auto reads =
	    makeReads(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
	inline static constexpr auto writes =
	    makeWrites(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

} // namespace telemetry::detail

#endif
