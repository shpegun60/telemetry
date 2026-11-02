/*
 * @file StaticClient.hpp
 * @brief Optional typed mixed batches with explicit caller-owned DTO storage.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_BATCH_STATIC_CLIENT_HPP
#define TELEMETRY_INTEGRATION_BATCH_STATIC_CLIENT_HPP
#pragma once

#include <client/DeviceClient.hpp>
#include <wire/BatchWire.hpp>
#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace telemetry::integration::batch {
namespace static_wire = ::telemetry::protocol::encoded::batch;

enum class StaticValueStatus : std::uint8_t {
	Ok,
	Stale,
	NoValue,
	InvalidPayload
};

namespace detail {
struct StaticBatchAccess;

template<class E>
concept StaticEndpoint = std::is_trivially_copyable_v<E> && requires(const E& endpoint) {
	{ endpoint.id } -> std::same_as<const std::uint32_t&>;
	E::category;
};

template<class E, client::Operation O, class Q, class R>
struct StaticItem {
	using Endpoint = E;
	using Request = Q;
	using Response = R;
	static constexpr client::Operation operation = O;
	E endpoint;
	const Q* request;
};

template<class T>
concept StaticItemType = std::is_trivially_copyable_v<T> && requires {
	typename T::Endpoint;
	typename T::Request;
	typename T::Response;
	requires StaticEndpoint<typename T::Endpoint>;
	requires(T::operation == client::Operation::FieldRead ||
	         T::operation == client::Operation::FieldWrite ||
	         T::operation == client::Operation::Command ||
	         T::operation == client::Operation::Service);
};

template<class Codec, class T>
inline constexpr std::size_t staticBytes = [] {
	if constexpr (std::is_void_v<T>)
		return std::size_t{0};
	else
		return Codec::template wireSize<T>;
}();

template<class T>
struct StaticSlot {
	static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_default_constructible_v<T>,
	              "Static Batch responses require trivial DTO storage and noexcept initialization");
	T value{};
	// Private unpublished records are all-zero; complete() fills them before exposure.
	client::ResultView operation{client::Status::Ok, client::Operation{}, 0, 0, 0, {}};
	bool available = false;
};

template<>
struct StaticSlot<void> {
	client::ResultView operation{client::Status::Ok, client::Operation{}, 0, 0, 0, {}};
	bool available = false;
};
} // namespace detail

template<detail::StaticEndpoint E>
    requires(static_cast<unsigned>(E::category) == 1 &&
             requires(const E& endpoint) {
	             typename E::Value;
	             requires(!std::is_void_v<typename E::Value>);
	             { endpoint.readable() } noexcept -> std::same_as<bool>;
             })
[[nodiscard]] auto read(E endpoint) noexcept
{
	return detail::StaticItem<E, client::Operation::FieldRead, void, typename E::Value>{endpoint,
	                                                                                    nullptr};
}

template<detail::StaticEndpoint E, class Q>
    requires(static_cast<unsigned>(E::category) == 1 &&
             std::same_as<std::remove_cvref_t<Q>, typename E::Value> &&
             std::is_lvalue_reference_v<Q &&> &&
             requires(const E& endpoint) {
	             { endpoint.writable() } noexcept -> std::same_as<bool>;
             })
[[nodiscard]] auto write(E endpoint, Q&& value) noexcept
{
	return detail::StaticItem<E, client::Operation::FieldWrite, typename E::Value, void>{
	    endpoint, std::addressof(value)};
}

template<detail::StaticEndpoint E, class Q>
    requires(static_cast<unsigned>(E::category) == 2 && !std::is_void_v<typename E::Request> &&
             std::same_as<std::remove_cvref_t<Q>, typename E::Request> &&
             std::is_lvalue_reference_v<Q &&>)
[[nodiscard]] auto command(E endpoint, Q&& value) noexcept
{
	return detail::StaticItem<E, client::Operation::Command, typename E::Request, void>{
	    endpoint, std::addressof(value)};
}

template<detail::StaticEndpoint E>
    requires(static_cast<unsigned>(E::category) == 2 && std::is_void_v<typename E::Request>)
[[nodiscard]] auto command(E endpoint) noexcept
{
	return detail::StaticItem<E, client::Operation::Command, void, void>{endpoint, nullptr};
}

template<detail::StaticEndpoint E, class Q>
    requires(static_cast<unsigned>(E::category) == 3 && !std::is_void_v<typename E::Request> &&
             std::same_as<std::remove_cvref_t<Q>, typename E::Request> &&
             std::is_lvalue_reference_v<Q &&>)
[[nodiscard]] auto service(E endpoint, Q&& value) noexcept
{
	return detail::StaticItem<E, client::Operation::Service, typename E::Request,
	                          typename E::Response>{endpoint, std::addressof(value)};
}

template<detail::StaticEndpoint E>
    requires(static_cast<unsigned>(E::category) == 3 && std::is_void_v<typename E::Request>)
[[nodiscard]] auto service(E endpoint) noexcept
{
	return detail::StaticItem<E, client::Operation::Service, void, typename E::Response>{endpoint,
	                                                                                     nullptr};
}

// Endpoint metadata is copied. Arguments stay stable through synchronous encoding;
// later callable moves and accepted completion no longer borrow the arguments.
template<detail::StaticItemType... I>
struct StaticBatchItems {
	std::tuple<I...> entries;
};

template<detail::StaticItemType... I>
    requires(sizeof...(I) > 0 && sizeof...(I) <= static_wire::maxItems)
[[nodiscard]] auto items(I... selected) noexcept
{
	return StaticBatchItems<I...>{{selected...}};
}

template<class... I>
class StaticBatchBinding {
public:
	[[nodiscard]] explicit operator bool() const noexcept
	{
		return owner_ != nullptr;
	}

private:
	const void* owner_ = nullptr;
	const client::Backend* backend_ = nullptr;
	bool (*supported_)(const client::Backend&) noexcept = nullptr;
	std::array<std::uint32_t, sizeof...(I)> ids_{};
	std::uint32_t session_ = 0;
	friend struct detail::StaticBatchAccess;
};

template<class... I>
struct StaticBatchBindingResult {
	client::Status status = client::Status::InvalidArgument;
	StaticBatchBinding<I...> binding{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == client::Status::Ok && bool(binding);
	}
};

namespace detail {
template<class Codec, class Selection>
class StaticBatchStorageImpl;
}
template<class Codec, class Selection>
using StaticBatchStorage = detail::StaticBatchStorageImpl<Codec, std::remove_cvref_t<Selection>>;
template<class Codec, class... I>
class StaticBatchResult;

template<class T>
class StaticBatchItemView {
public:
	client::ResultView operation{};
	StaticValueStatus valueStatus = StaticValueStatus::NoValue;

	[[nodiscard]] bool hasValue() const noexcept
	{
		return !std::is_void_v<T> && valueStatus == StaticValueStatus::Ok && value_;
	}

	[[nodiscard]] decltype(auto) value() const noexcept
	    requires(!std::is_void_v<T>)
	{
		if (!hasValue())
			std::abort();
		return (value_->value);
	}

private:
	const detail::StaticSlot<T>* value_ = nullptr;
	friend struct detail::StaticBatchAccess;
};

// Explicitly placed storage, sized from the selected types rather than protocol maxima.
// One in-flight operation per store; it stays busy through callable destruction.
template<class Codec, detail::StaticItemType... I>
class detail::StaticBatchStorageImpl<Codec, StaticBatchItems<I...>> {
	static_assert(sizeof...(I) > 0 && sizeof...(I) <= static_wire::maxItems);
	static_assert(
	    ((detail::staticBytes<Codec, typename I::Request> <= static_wire::maxRequestPayload &&
	      detail::staticBytes<Codec, typename I::Response> <= static_wire::maxReplyPayload) &&
	     ...),
	    "Static Batch DTO exceeds the bounded wire format");

public:
	static constexpr std::size_t encodedBytes =
	    (std::size_t{0} + ... + detail::staticBytes<Codec, typename I::Request>);
	static constexpr std::size_t requestBytes = 2 + 8 * sizeof...(I) + encodedBytes;
	static constexpr std::size_t responseBytes =
	    (std::size_t{2} + ... + (12 + detail::staticBytes<Codec, typename I::Response>));
	static_assert(requestBytes <= static_wire::maxRequestPayload &&
	                  responseBytes <= static_wire::maxReplyPayload,
	              "Static Batch payload exceeds the bounded wire format");
	StaticBatchStorageImpl() noexcept = default;
	StaticBatchStorageImpl(const StaticBatchStorageImpl&) = delete;
	StaticBatchStorageImpl& operator=(const StaticBatchStorageImpl&) = delete;

	// Requires completed drain and destroyed completion closures, like the client.
	~StaticBatchStorageImpl() = default;

	[[nodiscard]] bool busy() const noexcept
	{
		return busy_;
	}

private:
	std::array<std::byte, encodedBytes> encoded_{};
	std::array<std::byte, requestBytes> request_{};
	std::optional<StaticBatchItems<I...>> prepared_{};
	StaticBatchBinding<I...> binding_{};
	std::tuple<detail::StaticSlot<typename I::Response>...> slots_{};
	StaticValueStatus valueStatus_{};
	bool busy_ = false, haveResults_ = false;
	friend struct detail::StaticBatchAccess;
	friend class StaticBatchResult<Codec, I...>;
};

template<class Codec, class... I>
class StaticBatchResult {
public:
	client::ResultView operation{};
	StaticValueStatus valueStatus = StaticValueStatus::NoValue;
	static constexpr std::size_t count = sizeof...(I);
	template<std::size_t Index>
	[[nodiscard]] auto get() const noexcept;

private:
	const StaticBatchStorage<Codec, StaticBatchItems<I...>>* storage_ = nullptr;
	friend struct detail::StaticBatchAccess;
};

namespace detail {
struct StaticBatchAccess {
	template<class Client, class... I>
	[[nodiscard]] static bool current(const Client& owner,
	                                  const StaticBatchBinding<I...>& binding) noexcept
	{
		return binding.owner_ == &owner && binding.backend_ &&
		       owner.usesBackend(*binding.backend_) &&
		       owner.submissionStatus() == client::Status::Ok &&
		       owner.session() == binding.session_;
	}

	template<class Codec, class Item>
	[[nodiscard]] static client::Status contract(const Item& item) noexcept
	{
		const auto& endpoint = item.endpoint;
		using E = typename Item::Endpoint;
		constexpr auto op = Item::operation;
		static_assert((op == client::Operation::FieldRead || op == client::Operation::FieldWrite)
		                  ? static_cast<unsigned>(E::category) == 1
		                  : (op == client::Operation::Command
		                         ? static_cast<unsigned>(E::category) == 2
		                         : static_cast<unsigned>(E::category) == 3),
		              "Static Batch operation must match endpoint category");
		if constexpr (op == client::Operation::FieldRead) {
			if (!endpoint.readable())
				return client::Status::InvalidArgument;
		} else if constexpr (op == client::Operation::FieldWrite) {
			if (!endpoint.writable())
				return client::Status::ReadOnly;
		}
		if constexpr (requires { endpoint.fingerprint; })
			if (endpoint.fingerprint != Codec::fingerprint)
				return client::Status::NotAdmitted;
		if constexpr (requires { endpoint.valueBytes; }) {
			constexpr auto expected = op == client::Operation::FieldRead
			                              ? staticBytes<Codec, typename Item::Response>
			                              : staticBytes<Codec, typename Item::Request>;
			if (endpoint.valueBytes != expected)
				return client::Status::InvalidArgument;
		}
		if constexpr (requires { endpoint.requestBytes; })
			if (endpoint.requestBytes != staticBytes<Codec, typename Item::Request>)
				return client::Status::InvalidArgument;
		if constexpr (requires { endpoint.responseBytes; })
			if (endpoint.responseBytes != staticBytes<Codec, typename Item::Response>)
				return client::Status::InvalidArgument;
		if constexpr (!std::is_void_v<typename Item::Request>)
			if (!item.request)
				return client::Status::InvalidArgument;
		return client::Status::Ok;
	}

	template<class Codec, class Client, class B, class... I>
	[[nodiscard]] static auto bind(Client& owner, B& backend, static_wire::Capability capability,
	                               const StaticBatchItems<I...>& selection) noexcept
	{
		using Result = StaticBatchBindingResult<I...>;
		using Store = StaticBatchStorage<Codec, StaticBatchItems<I...>>;
		static_assert(requires { Codec::fingerprint; }, "Static Batch requires Codec::fingerprint");
		const auto gate = owner.submissionStatus();
		if (gate != client::Status::Ok)
			return Result{gate, {}};
		if constexpr (!requires(const B& value) {
			              { value.supportsBatch() } noexcept -> std::same_as<bool>;
		              }) {
			return Result{client::Status::Unsupported, {}};
		} else {
			if (!owner.usesBackend(backend) || !backend.supportsBatch())
				return Result{client::Status::Unsupported, {}};
			if (!static_wire::valid(capability) || sizeof...(I) > capability.itemLimit)
				return Result{};
			if (capability.session != owner.session() ||
			    capability.fingerprint != Codec::fingerprint)
				return Result{client::Status::NotAdmitted, {}};
			if (Store::requestBytes > capability.requestLimit ||
			    Store::responseBytes > capability.replyLimit)
				return Result{client::Status::BufferTooSmall, {}};
			const auto frozen = selection;
			Result result{client::Status::Ok, {}};
			auto& binding = result.binding;
			binding.backend_ = &backend;
			binding.session_ = capability.session;
			binding.supported_ = +[](const client::Backend& value) noexcept {
				return static_cast<const B&>(value).supportsBatch();
			};
			std::size_t index = 0;
			std::apply(
			    [&](const auto&... item) noexcept {
				    (
				        [&] {
					        const auto status = contract<Codec>(item);
					        if (result.status == client::Status::Ok)
						        result.status = status;
					        binding.ids_[index++] = item.endpoint.id;
				        }(),
				        ...);
			    },
			    frozen.entries);
			const auto finalGate = owner.submissionStatus();
			if (finalGate != client::Status::Ok)
				return Result{finalGate, {}};
			if (owner.session() != capability.session)
				return Result{client::Status::NotAdmitted, {}};
			if (result.status != client::Status::Ok)
				return Result{result.status, {}};
			binding.owner_ = &owner;
			return result;
		}
	}

	template<class Store>
	struct Lease {
		Store* storage;

		explicit Lease(Store& value) noexcept : storage(&value)
		{
			value.busy_ = true;
		}

		Lease(Lease&& other) noexcept : storage(std::exchange(other.storage, nullptr))
		{}

		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;

		~Lease()
		{
			if (storage)
				storage->busy_ = false;
		}
	};

	template<class Codec, class Client, class... I>
	static void complete(Client& owner, StaticBatchStorage<Codec, StaticBatchItems<I...>>& storage,
	                     const client::ResultView& operation) noexcept
	{
		storage.haveResults_ = false;
		storage.valueStatus_ = current(owner, storage.binding_) ? StaticValueStatus::NoValue
		                                                        : StaticValueStatus::Stale;
		std::apply(
		    [](auto&... slot) noexcept {
			    ((slot.available = false), ...);
		    },
		    storage.slots_);
		if (!operation.succeeded())
			return;
		std::array<static_wire::RequestItem, sizeof...(I)> expected{};
		std::size_t index = 0;
		((expected[index] = {static_cast<protocol::encoded::Operation>(I::operation),
		                     storage.binding_.ids_[index],
		                     {}},
		  ++index),
		 ...);
		const auto parsed = static_wire::parseReply(operation.payload, expected);
		storage.valueStatus_ = StaticValueStatus::InvalidPayload;
		if (operation.operation != client::Operation::Batch || !parsed)
			return;
		bool lengths = true;
		[&]<std::size_t... N>(std::index_sequence<N...>) noexcept {
			(
			    [&] {
				    auto& destination = std::get<N>(storage.slots_);
				    const auto& item = parsed.items[N];
				    destination.operation = {
				        client::Status::Ok, I::operation, item.dispatch, item.endpointStatus, 0,
				        item.payload};
				    if (destination.operation.succeeded() &&
				        item.payload.size() != staticBytes<Codec, typename I::Response>)
					    lengths = false;
			    }(),
			    ...);
		}(std::index_sequence_for<I...>{});
		if (!lengths)
			return;
		storage.haveResults_ = true;
		if (!current(owner, storage.binding_)) {
			storage.valueStatus_ = StaticValueStatus::Stale;
			return;
		}
		bool valid = true;
		std::apply(
		    [&](auto&... slot) noexcept {
			    (
			        [&] {
				        using T = typename I::Response;
				        if constexpr (!std::is_void_v<T>)
					        if (slot.operation.succeeded() &&
					            Codec::decode(slot.operation.payload, slot.value) != 0)
						        valid = false;
			        }(),
			        ...);
		    },
		    storage.slots_);
		if (!current(owner, storage.binding_)) {
			storage.valueStatus_ = StaticValueStatus::Stale;
			return;
		}
		if (!valid)
			return;
		storage.valueStatus_ = StaticValueStatus::Ok;
		std::apply(
		    [](auto&... slot) noexcept {
			    ((slot.available = slot.operation.succeeded()), ...);
		    },
		    storage.slots_);
	}

	template<class Codec, class Client, class F, class... I>
	struct Completion {
		using Store = StaticBatchStorage<Codec, StaticBatchItems<I...>>;
		Lease<Store> lease;
		F callable;
		Client* owner;
		Store* storage;

		void operator()(const client::ResultView& operation) noexcept
		{
			complete<Codec, Client, I...>(*owner, *storage, operation);
			StaticBatchResult<Codec, I...> result;
			result.operation = operation;
			result.valueStatus = storage->valueStatus_;
			result.storage_ = storage;
			callable(result);
		}
	};

	template<class Codec, std::size_t N, std::size_t P, std::size_t C, class Client, class... I,
	         class F>
	[[nodiscard]] static client::Submission
	submit(Client& owner, const StaticBatchBinding<I...>& binding,
	       const StaticBatchItems<I...>& selection,
	       StaticBatchStorage<Codec, StaticBatchItems<I...>>& storage, F&& callback,
	       std::uint32_t timeout) noexcept
	{
		using Store = StaticBatchStorage<Codec, StaticBatchItems<I...>>;
		using Fn = std::decay_t<F>;
		using Done = Completion<Codec, Client, Fn, I...>;
		static_assert(std::is_nothrow_invocable_v<Fn&, const StaticBatchResult<Codec, I...>&>,
		              "Static Batch completion must be noexcept");
		static_assert(std::is_nothrow_constructible_v<Fn, F&&> &&
		                  std::is_nothrow_move_constructible_v<Fn> &&
		                  std::is_nothrow_destructible_v<Fn>,
		              "Static Batch completion ownership must not throw");
		static_assert(sizeof(Done) <= C,
		              "Static Batch capture exceeds CallbackBytes; choose capacity explicitly");
		const auto gate = owner.submissionStatus();
		if (gate != client::Status::Ok)
			return {gate, {}};
		if (storage.busy_)
			return {client::Status::Busy, {}};
		if (!timeout || timeout > client::maxTimeoutMs)
			return {client::Status::InvalidArgument, {}};
		if (!current(owner, binding))
			return {client::Status::NotAdmitted, {}};
		if (!binding.supported_ || !binding.supported_(*binding.backend_))
			return {client::Status::Unsupported, {}};
		if constexpr (std::is_pointer_v<std::remove_reference_t<F>>)
			if (!callback)
				return {client::Status::InvalidArgument, {}};
		if (owner.pending() >= N)
			return {client::Status::QueueFull, {}};
		if (Store::requestBytes > P)
			return {client::Status::PayloadFull, {}};
		Lease<Store> lease{storage};
		storage.binding_ = binding;
		storage.prepared_.emplace(selection);
		storage.haveResults_ = false;
		std::apply(
		    [](auto&... slot) noexcept {
			    ((slot.available = false), ...);
		    },
		    storage.slots_);
		client::Status status = client::Status::Ok;
		std::size_t index = 0;
		std::apply(
		    [&](const auto&... item) noexcept {
			    (
			        [&] {
				        const auto checked = contract<Codec>(item);
				        if (status == client::Status::Ok)
					        status = checked;
				        if (item.endpoint.id != storage.binding_.ids_[index++])
					        status = client::Status::InvalidArgument;
			        }(),
			        ...);
		    },
		    storage.prepared_->entries);
		if (status != client::Status::Ok)
			return {status, {}};
		if (!current(owner, storage.binding_))
			return {owner.submissionStatus() == client::Status::Ok ? client::Status::NotAdmitted
			                                                       : owner.submissionStatus(),
			        {}};
		std::array<static_wire::RequestItem, sizeof...(I)> requests{};
		index = 0;
		std::size_t cursor = 0;
		std::apply(
		    [&](const auto&... item) noexcept {
			    (
			        [&] {
				        using T = typename std::remove_cvref_t<decltype(item)>::Request;
				        constexpr auto length = staticBytes<Codec, T>;
				        const auto output = std::span{storage.encoded_}.subspan(cursor, length);
				        if constexpr (!std::is_void_v<T>)
					        if (status == client::Status::Ok &&
					            Codec::encode(*item.request, output) != 0)
						        status = client::Status::CodecError;
				        requests[index] = {static_cast<protocol::encoded::Operation>(I::operation),
				                           storage.binding_.ids_[index], output};
				        ++index;
				        cursor += length;
			        }(),
			        ...);
		    },
		    storage.prepared_->entries);
		if (status != client::Status::Ok)
			return {status, {}};
		const auto encodedGate = owner.submissionStatus();
		if (encodedGate != client::Status::Ok)
			return {encodedGate, {}};
		if (!current(owner, storage.binding_))
			return {client::Status::NotAdmitted, {}};
		const auto written = static_wire::writeRequest(storage.request_, requests);
		if (!written)
			return {client::Status::InvalidArgument, {}};
		auto completion = Done{std::move(lease), std::forward<F>(callback), &owner, &storage};
		const auto capturedGate = owner.submissionStatus();
		if (capturedGate != client::Status::Ok)
			return {capturedGate, {}};
		if (!current(owner, storage.binding_))
			return {client::Status::NotAdmitted, {}};
		return owner.submitEncoded({client::Operation::Batch, 0,
		                            client::Input{storage.request_}.first(written.written),
		                            static_cast<std::uint32_t>(Store::responseBytes)},
		                           std::move(completion), timeout);
	}

	template<class T>
	[[nodiscard]] static StaticBatchItemView<T> view(const StaticSlot<T>& slot,
	                                                 StaticValueStatus overall) noexcept
	{
		StaticBatchItemView<T> result;
		result.operation = slot.operation;
		result.valueStatus =
		    overall == StaticValueStatus::Ok
		        ? (!std::is_void_v<T> && slot.available ? StaticValueStatus::Ok
		                                                : StaticValueStatus::NoValue)
		        : overall;
		if (overall == StaticValueStatus::Ok && slot.available && !std::is_void_v<T>)
			result.value_ = &slot;
		return result;
	}
};
} // namespace detail

template<class Codec, class... I>
template<std::size_t Index>
auto StaticBatchResult<Codec, I...>::get() const noexcept
{
	using Item = std::tuple_element_t<Index, std::tuple<I...>>;
	using View = StaticBatchItemView<typename Item::Response>;
	if (!storage_ || !storage_->haveResults_)
		return std::optional<View>{};
	return std::optional<View>{
	    detail::StaticBatchAccess::view(std::get<Index>(storage_->slots_), valueStatus)};
}

template<class Codec, std::size_t N, std::size_t P, std::size_t C, std::size_t S, std::size_t A,
         class Lock, class Policy, class B, detail::StaticItemType... I>
    requires(Policy::allowTyped && std::is_convertible_v<B*, client::Backend*>)
[[nodiscard]] auto bindBatch(client::DeviceClient<Codec, N, P, C, S, A, Lock, Policy>& owner,
                             B& backend, static_wire::Capability capability,
                             const StaticBatchItems<I...>& selection) noexcept
{
	return detail::StaticBatchAccess::bind<Codec>(owner, backend, capability, selection);
}

template<class Codec, std::size_t N, std::size_t P, std::size_t C, std::size_t S, std::size_t A,
         class Lock, class Policy, class... I, class F>
    requires Policy::allowTyped
[[nodiscard]] client::Submission
batch(client::DeviceClient<Codec, N, P, C, S, A, Lock, Policy>& owner,
      const StaticBatchBinding<I...>& binding, const StaticBatchItems<I...>& selection,
      StaticBatchStorage<Codec, StaticBatchItems<I...>>& storage, F&& callback,
      std::uint32_t timeoutMs = 30000) noexcept
{
	return detail::StaticBatchAccess::submit<Codec, N, P, C>(owner, binding, selection, storage,
	                                                         std::forward<F>(callback), timeoutMs);
}

} // namespace telemetry::integration::batch
#endif // TELEMETRY_INTEGRATION_BATCH_STATIC_CLIENT_HPP
