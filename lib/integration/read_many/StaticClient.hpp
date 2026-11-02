/*
 * @file StaticClient.hpp
 * @brief Optional typed scalar ReadMany over the existing static client FIFO.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_READ_MANY_STATIC_CLIENT_HPP
#define TELEMETRY_INTEGRATION_READ_MANY_STATIC_CLIENT_HPP
#pragma once

#include <client/DeviceClient.hpp>
#include <wire/ReadManyWire.hpp>
#include <array>
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::integration::read_many {
namespace static_wire = ::telemetry::protocol::encoded::read_many;

enum class StaticValueStatus : std::uint8_t {
	Ok,
	Stale,
	NoValue,
	InvalidPayload
};

namespace detail {
struct StaticReadManyAccess;

template<class T>
inline constexpr bool staticScalar = [] {
	if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
		return sizeof(T) <= 8;
	else
		return std::is_same_v<T, float> || std::is_same_v<T, double>;
}();

template<class E>
concept StaticField = requires(const E& endpoint) {
	typename E::Value;
	requires(static_cast<unsigned>(E::category) == 1);
	requires staticScalar<typename E::Value>;
	requires std::is_trivially_copyable_v<E>;
	{ endpoint.id } -> std::same_as<const std::uint32_t&>;
	{ endpoint.readable() } noexcept -> std::same_as<bool>;
};
} // namespace detail

// This owns endpoint metadata, never a reference to a temporary endpoint array.
template<detail::StaticField... E>
struct StaticFields {
	std::tuple<E...> endpoints;
};

template<detail::StaticField... E>
    requires(sizeof...(E) > 0 && sizeof...(E) <= static_wire::maxItems)
[[nodiscard]] constexpr StaticFields<E...> fields(E... endpoints) noexcept
{
	return {{endpoints...}};
}

// Bindings and values cannot outlive their client. Backend sessions must not be
// reused during that client's lifetime; the Encoded backends enforce increasing
// generations. Custom backends must enforce the same admission precondition.
template<class... T>
class StaticReadManyBinding {
public:
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return owner_ != nullptr;
	}

private:
	const void* owner_ = nullptr;
	const client::Backend* backend_ = nullptr;
	bool (*supported_)(const client::Backend&) noexcept = nullptr;
	std::array<std::uint32_t, sizeof...(T)> ids_{};
	std::uint32_t session_ = 0, responseBytes_ = 0;
	friend struct detail::StaticReadManyAccess;
};

template<class... T>
struct StaticReadManyBindingResult {
	client::Status status = client::Status::InvalidArgument;
	StaticReadManyBinding<T...> binding{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == client::Status::Ok && bool(binding);
	}
};

template<class T>
class StaticReadManyItem {
public:
	std::uint32_t id = 0;
	static_wire::ItemStatus status = static_wire::ItemStatus::ReadError;
	StaticValueStatus valueStatus = StaticValueStatus::NoValue;

	[[nodiscard]] bool hasValue() const noexcept
	{
		return valueStatus == StaticValueStatus::Ok && status == static_wire::ItemStatus::Ok;
	}

	[[nodiscard]] const T& value() const noexcept
	{
		if (!hasValue())
			std::abort();
		return value_;
	}

private:
	T value_{};
	friend struct detail::StaticReadManyAccess;
};

template<class... T>
class StaticReadManyResult {
public:
	client::ResultView operation{};
	StaticValueStatus valueStatus = StaticValueStatus::Stale;
	static constexpr std::size_t count = sizeof...(T);

	template<std::size_t I>
	[[nodiscard]] const auto& get() const noexcept
	{
		return std::get<I>(items_);
	}

private:
	std::tuple<StaticReadManyItem<T>...> items_{};
	friend struct detail::StaticReadManyAccess;
};

namespace detail {
struct StaticReadManyAccess {
	template<class Client, class... T>
	[[nodiscard]] static bool current(const Client& owner,
	                                  const StaticReadManyBinding<T...>& binding) noexcept
	{
		return binding.owner_ == &owner && binding.backend_ &&
		       owner.usesBackend(*binding.backend_) &&
		       owner.submissionStatus() == client::Status::Ok &&
		       owner.session() == binding.session_;
	}

	template<class Codec, class Client, class B, class... E>
	[[nodiscard]] static auto bind(Client& owner, B& backend, static_wire::Capability capability,
	                               const StaticFields<E...>& selection) noexcept
	{
		using Result = StaticReadManyBindingResult<typename E::Value...>;
		static_assert(
		    requires { Codec::fingerprint; },
		    "Static ReadMany requires Codec::fingerprint matching descriptorHeader()");
		static_assert(((Codec::template wireSize<typename E::Value> == 1 ||
		                Codec::template wireSize<typename E::Value> == 2 ||
		                Codec::template wireSize<typename E::Value> == 4 ||
		                Codec::template wireSize<typename E::Value> == 8) &&
		               ...),
		              "ReadMany supports scalar/enum wire widths 1, 2, 4 or 8");
		static_assert(
		    ((Codec::template wireSize<typename E::Value> == sizeof(typename E::Value)) && ...),
		    "ReadMany requires canonical native scalar widths");
		const auto gate = owner.submissionStatus();
		if (gate != client::Status::Ok)
			return Result{gate, {}};
		if constexpr (!requires(const B& value) {
			              { value.supportsReadMany() } noexcept -> std::same_as<bool>;
		              }) {
			return Result{client::Status::Unsupported, {}};
		} else {
			if (!owner.usesBackend(backend) || !backend.supportsReadMany())
				return Result{client::Status::Unsupported, {}};
			if (!static_wire::valid(capability))
				return Result{client::Status::InvalidArgument, {}};
			if (capability.session != owner.session() ||
			    capability.fingerprint != Codec::fingerprint)
				return Result{client::Status::NotAdmitted, {}};
			constexpr std::size_t count = sizeof...(E);
			if (count > capability.itemLimit)
				return Result{client::Status::InvalidArgument, {}};
			constexpr auto response =
			    (std::size_t{2} + ... + (8 + Codec::template wireSize<typename E::Value>));
			if (2 + 4 * count > capability.requestLimit || response > capability.replyLimit)
				return Result{client::Status::BufferTooSmall, {}};
			Result result{client::Status::Ok, {}};
			auto& binding = result.binding;
			binding.backend_ = &backend;
			binding.session_ = capability.session;
			binding.responseBytes_ = static_cast<std::uint32_t>(response);
			binding.supported_ = +[](const client::Backend& value) noexcept {
				return static_cast<const B&>(value).supportsReadMany();
			};
			std::size_t index = 0;
			std::apply(
			    [&](const auto&... endpoint) noexcept {
				    (
				        [&] {
					        using Field = std::remove_cvref_t<decltype(endpoint)>;
					        if (!endpoint.readable())
						        result.status = client::Status::InvalidArgument;
					        if constexpr (requires { endpoint.fingerprint; })
						        if (endpoint.fingerprint != Codec::fingerprint)
							        result.status = client::Status::NotAdmitted;
					        if constexpr (requires { endpoint.valueBytes; })
						        if (endpoint.valueBytes !=
						            Codec::template wireSize<typename Field::Value>)
							        result.status = client::Status::InvalidArgument;
					        binding.ids_[index++] = endpoint.id;
				        }(),
				        ...);
			    },
			    selection.endpoints);
			for (std::size_t i = 0; i < count; ++i)
				for (std::size_t j = 0; j < i; ++j)
					if (binding.ids_[i] == binding.ids_[j])
						result.status = client::Status::InvalidArgument;
			const auto capturedGate = owner.submissionStatus();
			if (capturedGate != client::Status::Ok)
				return Result{capturedGate, {}};
			if (owner.session() != capability.session)
				return Result{client::Status::NotAdmitted, {}};
			if (result.status != client::Status::Ok)
				return Result{result.status, {}};
			binding.owner_ = &owner;
			return result;
		}
	}

	template<class Codec, class Client, class... T>
	static void complete(const Client& owner, const StaticReadManyBinding<T...>& binding,
	                     const client::ResultView& operation,
	                     StaticReadManyResult<T...>& result) noexcept
	{
		result.operation = operation;
		if (!current(owner, binding))
			return;
		result.valueStatus = StaticValueStatus::NoValue;
		if (!operation.succeeded())
			return;
		result.valueStatus = StaticValueStatus::InvalidPayload;
		if (operation.operation != client::Operation::ReadMany)
			return;
		const auto parsed = static_wire::parseReply(operation.payload, binding.ids_);
		if (!parsed)
			return;
		bool valid = true;
		[&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
			(
			    [&] {
				    auto& destination = std::get<I>(result.items_);
				    const auto& item = parsed.items[I];
				    destination.id = item.id;
				    destination.status = item.status;
				    if (item.status == static_wire::ItemStatus::Ok &&
				        (item.payload.size() != Codec::template wireSize<T> ||
				         Codec::decode(item.payload, destination.value_) != 0))
					    valid = false;
			    }(),
			    ...);
		}(std::index_sequence_for<T...>{});
		if (!current(owner, binding)) {
			result.valueStatus = StaticValueStatus::Stale;
			return;
		}
		if (!valid)
			return;
		result.valueStatus = StaticValueStatus::Ok;
		std::apply(
		    [](auto&... item) noexcept {
			    ((item.valueStatus = item.status == static_wire::ItemStatus::Ok
			                             ? StaticValueStatus::Ok
			                             : StaticValueStatus::NoValue),
			     ...);
		    },
		    result.items_);
	}

	template<class Codec, std::size_t C, class Client, class... T, class F>
	[[nodiscard]] static client::Submission submit(Client& owner,
	                                               const StaticReadManyBinding<T...>& binding,
	                                               F&& callback, std::uint32_t timeout) noexcept
	{
		using Callback = std::decay_t<F>;
		static_assert(std::is_nothrow_invocable_v<Callback&, const StaticReadManyResult<T...>&>,
		              "Static ReadMany completion must be noexcept");
		static_assert(std::is_nothrow_constructible_v<Callback, F&&>,
		              "Static ReadMany completion ownership must not throw");
		const auto gate = owner.submissionStatus();
		if (gate != client::Status::Ok)
			return {gate, {}};
		if (!timeout || timeout > client::maxTimeoutMs)
			return {client::Status::InvalidArgument, {}};
		if (!current(owner, binding))
			return {client::Status::NotAdmitted, {}};
		if (!binding.supported_ || !binding.supported_(*binding.backend_))
			return {client::Status::Unsupported, {}};
		if constexpr (std::is_pointer_v<std::remove_reference_t<F>>)
			if (!callback)
				return {client::Status::InvalidArgument, {}};
		const auto frozen = binding;
		std::array<std::byte, 2 + 4 * sizeof...(T)> payload{};
		const auto written = static_wire::writeRequest(payload, frozen.ids_);
		if (!written)
			return {client::Status::InvalidArgument, {}};
		// Freeze every normalized contract before user callback ownership changes.
		auto completion = [client = &owner, frozen, fn = std::forward<F>(callback)](
		                      const client::ResultView& operation) mutable noexcept {
			StaticReadManyResult<T...> result;
			complete<Codec>(*client, frozen, operation, result);
			fn(result);
		};
		static_assert(sizeof(completion) <= C,
		              "Static ReadMany capture exceeds CallbackBytes; select capacity explicitly");
		const auto capturedGate = owner.submissionStatus();
		if (capturedGate != client::Status::Ok)
			return {capturedGate, {}};
		if (!current(owner, frozen))
			return {client::Status::NotAdmitted, {}};
		return owner.submitEncoded({client::Operation::ReadMany, 0,
		                            client::Input{payload}.first(written.written),
		                            frozen.responseBytes_},
		                           std::move(completion), timeout);
	}
};
} // namespace detail

// Owner-only optional adapters. Codec::fingerprint must describe the exact
// descriptorHeader() admitted by the client. Supplying TRM1 is an application
// assertion about this peer/session, not discovery or device authentication.
template<class Codec, std::size_t N, std::size_t P, std::size_t C, std::size_t S, std::size_t A,
         class Lock, class Policy, class B, detail::StaticField... E>
    requires(Policy::allowTyped && sizeof...(E) > 0 && sizeof...(E) <= static_wire::maxItems &&
             std::is_convertible_v<B*, client::Backend*>)
[[nodiscard]] auto bindReadMany(client::DeviceClient<Codec, N, P, C, S, A, Lock, Policy>& owner,
                                B& backend, static_wire::Capability capability,
                                const StaticFields<E...>& selection) noexcept
{
	return detail::StaticReadManyAccess::bind<Codec>(owner, backend, capability, selection);
}

template<class Codec, std::size_t N, std::size_t P, std::size_t C, std::size_t S, std::size_t A,
         class Lock, class Policy, class... T, class F>
    requires Policy::allowTyped
[[nodiscard]] client::Submission
readMany(client::DeviceClient<Codec, N, P, C, S, A, Lock, Policy>& owner,
         const StaticReadManyBinding<T...>& binding, F&& callback,
         std::uint32_t timeoutMs = 30000) noexcept
{
	return detail::StaticReadManyAccess::submit<Codec, C>(owner, binding, std::forward<F>(callback),
	                                                      timeoutMs);
}
} // namespace telemetry::integration::read_many
#endif // TELEMETRY_INTEGRATION_READ_MANY_STATIC_CLIENT_HPP
