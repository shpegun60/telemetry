/*
 * @file NativeCall.hpp
 * @brief Exact native request/result dispatch through positional thunk tables.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */

#ifndef TELEMETRY_DETAIL_NATIVE_CALL_HPP
#define TELEMETRY_DETAIL_NATIVE_CALL_HPP
#pragma once

#include "../result/NativeCallResult.hpp"
#include "../result/ServiceResult.hpp"
#include "../result/BorrowedServiceResult.hpp"
#include "../result/EndpointStatus.hpp"
#include <array>
#include <cstdint>
#include <tuple>
#include <type_traits>

namespace telemetry::detail {

template<class T>
inline constexpr bool nativeServiceResult = false;
template<class T>
inline constexpr bool nativeServiceResult<ServiceResult<T>> = true;
template<class T>
inline constexpr bool nativeServiceResult<BorrowedServiceResult<T>> = true;

// Void requests carry only a dummy nullptr value through the internal table.
// Nonvoid requests borrow the caller object for the synchronous operation.
template<class Request>
using NativeArgument = const std::conditional_t<std::is_void_v<Request>, std::nullptr_t, Request>&;

template<class Definition, bool Void = std::is_void_v<typename Definition::Request>>
struct NativeDefinitionResult {
	using type = decltype(std::declval<const Definition&>().call(
	    std::declval<const typename Definition::Request&>()));
};

template<class Definition>
struct NativeDefinitionResult<Definition, true> {
	using type = decltype(std::declval<const Definition&>().call());
};

// One specialization per table/request/result, reused between call sites.
// Failure branches do not invoke the endpoint or construct a payload.
// Public methods:
// - invoke(): Call matching definition.
// - make(): Build positional thunks.
template<class Tuple, class Result, class Request>
struct NativeCallDispatch {
	using Invoke = NativeCallResult<Result> (*)(const Tuple&, NativeArgument<Request>) noexcept;

	template<std::size_t I>
	static NativeCallResult<Result> invoke(const Tuple& definitions,
	                                       NativeArgument<Request> request) noexcept
	{
		using Definition = std::tuple_element_t<I, Tuple>;
		if constexpr (std::is_same_v<typename Definition::Request, Request> &&
		              std::is_same_v<typename NativeDefinitionResult<Definition>::type, Result>) {
			return NativeCallResult<Result>::successFrom([&]() noexcept -> Result {
				if constexpr (std::is_void_v<Request>)
					return std::get<I>(definitions).call();
				else
					return std::get<I>(definitions).call(request);
			});
		} else {
			return NativeCallResult<Result>::failure(NativeCallStatus::SignatureMismatch);
		}
	}

	template<std::size_t... I>
	static consteval auto make(std::index_sequence<I...>) noexcept
	{
		return std::array<Invoke, sizeof...(I)>{{&invoke<I>...}};
	}

	inline static constexpr auto entries =
	    make(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

// The global index selects a group, then the local table selects its row.
// No flattened table, native-value variant or additional owner is introduced.
// Public methods:
// - invoke(): Call selected group.
// - make(): Build group thunks.
template<class Tuple, class Result, class Request>
struct NativeGroupCallDispatch {
	using Invoke = NativeCallResult<Result> (*)(const Tuple&, std::uint32_t,
	                                            NativeArgument<Request>) noexcept;

	template<std::size_t I>
	static NativeCallResult<Result> invoke(const Tuple& groups, std::uint32_t entry,
	                                       NativeArgument<Request> request) noexcept
	{
		const auto& table = *std::get<I>(groups).table;
		if constexpr (std::is_same_v<Result, CommandResult>) {
			if constexpr (std::is_void_v<Request>)
				return table.callAs(entry);
			else
				return table.callAs(entry, request);
		} else {
			if constexpr (std::is_void_v<Request>)
				return table.template callAs<Result>(entry);
			else
				return table.template callAs<Result>(entry, request);
		}
	}

	template<std::size_t... I>
	static consteval auto make(std::index_sequence<I...>) noexcept
	{
		return std::array<Invoke, sizeof...(I)>{{&invoke<I>...}};
	}

	inline static constexpr auto entries =
	    make(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
};

} // namespace telemetry::detail

#endif
