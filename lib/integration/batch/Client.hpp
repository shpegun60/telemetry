/*
 * @file Client.hpp
 * @brief Optional mixed Encoded batches through DynamicClient's existing FIFO.
 * @author Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_BATCH_CLIENT_HPP
#define TELEMETRY_INTEGRATION_BATCH_CLIENT_HPP
#pragma once

#include <integration/dynamic/DynamicClient.hpp>
#include <wire/BatchWire.hpp>
#include <concepts>

namespace telemetry::integration::batch {
namespace wire = ::telemetry::protocol::encoded::batch;
using ValueStatus = dynamic::ValueStatus;

struct Endpoint {
	client::Operation operation = client::Operation::FieldRead;
	std::uint32_t id = 0;
};

namespace detail {
struct BatchAccess;
}

// The issuing client and its workspace object outlive bindings. All contracts
// are copied; source endpoint arrays need not remain alive after bindBatch.
class BatchBinding {
public:
	struct Entry {
		std::uint32_t id = 0;
		TypeId requestType = 0, responseType = 0;
		std::uint32_t requestBytes = 0, responseBytes = 0;
		client::Operation operation = client::Operation::FieldRead;
	};

	[[nodiscard]] std::span<const Entry> entries() const noexcept
	{
		return {entries_.data(), count_};
	}

	[[nodiscard]] std::uint32_t requestBytes() const noexcept
	{
		return requestBytes_;
	}

	[[nodiscard]] std::uint32_t responseBytes() const noexcept
	{
		return responseBytes_;
	}

	[[nodiscard]] std::uint32_t session() const noexcept
	{
		return session_;
	}

private:
	const void* client_ = nullptr;
	const runtime::RuntimeSchemaWorkspace* workspace_ = nullptr;
	bool (*supported_)(const client::Backend&) noexcept = nullptr;
	std::uint64_t epoch_ = 0, schemaGeneration_ = 0;
	wire::Capability capability_{};
	std::uint32_t session_ = 0, requestBytes_ = 0, responseBytes_ = 0;
	std::uint16_t count_ = 0;
	std::array<Entry, wire::maxItems> entries_{};
	friend struct detail::BatchAccess;
};

struct BatchBindingResult {
	client::Status status = client::Status::NotAdmitted;
	BatchBinding binding{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == client::Status::Ok;
	}
};

struct BatchItem {
	std::uint32_t id = 0;
	client::ResultView operation{};
	ValueStatus valueStatus = ValueStatus::NoValue;
	runtime::DynamicValueView value{};

	[[nodiscard]] bool hasValue() const noexcept
	{
		return valueStatus == ValueStatus::Ok && value.valid();
	}
};

// A successful outer operation means the complete batch reply arrived. Inspect
// every item's independent operation result. All payloads/views borrow backend
// storage only until the callback returns. Stale schemas suppress value views,
// while structurally checked per-item execution statuses remain available.
struct BatchResult {
	client::ResultView operation{};
	ValueStatus valueStatus = ValueStatus::Stale;
	std::uint16_t count = 0;
	std::array<BatchItem, wire::maxItems> items{};
};

namespace detail {
struct BatchAccess {
	template<class Client, class F>
	struct Completion {
		Client* owner;
		BatchBinding binding;
		F callable;

		void operator()(const client::ResultView& operation) noexcept
		{
			dynamic::detail::CallbackScope guard{owner->callbackActive_};
			const auto result = complete(*owner, binding, operation);
			callable(result);
		}
	};

	template<class Client>
	[[nodiscard]] static bool current(const Client& owner, const BatchBinding& binding) noexcept
	{
		return owner.isCurrent() && binding.client_ == &owner && binding.epoch_ == owner.epoch_ &&
		       binding.workspace_ == owner.state_.schema.owner() &&
		       binding.schemaGeneration_ == owner.state_.schema.generation() &&
		       binding.session_ == owner.state_.session;
	}

	template<class Client>
	[[nodiscard]] static bool contractsMatch(const Client& owner,
	                                         const BatchBinding& binding) noexcept
	{
		for (const auto& entry : binding.entries()) {
			descriptor::Category category;
			if (!owner.categoryFor(entry.operation, category))
				return false;
			const auto* endpoint = owner.find(category, entry.id);
			if (!endpoint ||
			    owner.permissionFor(entry.operation, endpoint->capabilities) != client::Status::Ok)
				return false;
			TypeId request, response;
			owner.contract(entry.operation, *endpoint, request, response);
			const auto* requestType = owner.state_.schema.type(request);
			const auto* responseType = owner.state_.schema.type(response);
			if (!requestType || !responseType || request != entry.requestType ||
			    response != entry.responseType || requestType->wireBytes != entry.requestBytes ||
			    responseType->wireBytes != entry.responseBytes)
				return false;
		}
		return true;
	}

	template<std::size_t N, std::size_t P, std::size_t C, class B>
	[[nodiscard]] static BatchBindingResult bind(dynamic::DynamicClient<N, P, C>& owner, B& backend,
	                                             wire::Capability capability,
	                                             std::span<const Endpoint> endpoints) noexcept
	{
		const auto gate = owner.submissionGate();
		if (gate != client::Status::Ok)
			return {gate, {}};
		if constexpr (!requires(const B& value) {
			              { value.supportsBatch() } noexcept -> std::same_as<bool>;
		              }) {
			return {client::Status::Unsupported, {}};
		} else {
			if (static_cast<const client::Backend*>(&backend) != &owner.backend_ ||
			    !backend.supportsBatch())
				return {client::Status::Unsupported, {}};
			if (!wire::valid(capability))
				return {client::Status::InvalidArgument, {}};
			if (capability.session != owner.state_.session ||
			    capability.fingerprint != owner.state_.schema.fingerprint())
				return {client::Status::NotAdmitted, {}};
			if (endpoints.empty() || endpoints.size() > capability.itemLimit)
				return {client::Status::InvalidArgument, {}};
			BatchBinding binding;
			binding.client_ = &owner;
			binding.workspace_ = owner.state_.schema.owner();
			binding.epoch_ = owner.epoch_;
			binding.schemaGeneration_ = owner.state_.schema.generation();
			binding.session_ = capability.session;
			binding.capability_ = capability;
			binding.count_ = static_cast<std::uint16_t>(endpoints.size());
			binding.supported_ = +[](const client::Backend& value) noexcept {
				return static_cast<const B&>(value).supportsBatch();
			};
			std::uint64_t requestBytes = 2, responseBytes = 2;
			for (std::size_t i = 0; i < endpoints.size(); ++i) {
				const auto endpoint = owner.bind(endpoints[i].operation, endpoints[i].id);
				if (!endpoint)
					return {endpoint.status, {}};
				const auto& value = endpoint.endpoint;
				binding.entries_[i] = {
				    value.id(),           value.requestType(),   value.responseType(),
				    value.requestBytes(), value.responseBytes(), value.operation()};
				requestBytes += 8 + std::uint64_t{value.requestBytes()};
				responseBytes += 12 + std::uint64_t{value.responseBytes()};
			}
			if (requestBytes > capability.requestLimit || responseBytes > capability.replyLimit)
				return {client::Status::BufferTooSmall, {}};
			binding.requestBytes_ = static_cast<std::uint32_t>(requestBytes);
			binding.responseBytes_ = static_cast<std::uint32_t>(responseBytes);
			return {client::Status::Ok, binding};
		}
	}

	template<class Client>
	[[nodiscard]] static BatchResult complete(const Client& owner, const BatchBinding& binding,
	                                          const client::ResultView& operation) noexcept
	{
		BatchResult result{operation};
		const bool admitted = current(owner, binding);
		result.valueStatus = admitted ? ValueStatus::NoValue : ValueStatus::Stale;
		if (!operation.succeeded())
			return result;
		if (operation.operation != client::Operation::Batch) {
			result.valueStatus = ValueStatus::TypeMismatch;
			return result;
		}
		std::array<wire::RequestItem, wire::maxItems> expected{};
		for (std::size_t i = 0; i < binding.count_; ++i)
			expected[i] = {static_cast<protocol::encoded::Operation>(binding.entries_[i].operation),
			               binding.entries_[i].id,
			               {}};
		const auto parsed =
		    wire::parseReply(operation.payload, std::span{expected}.first(binding.count_));
		if (!parsed) {
			result.valueStatus = ValueStatus::InvalidPayload;
			return result;
		}
		// Exact lengths use copied contracts, including after schema invalidation.
		// No item payload is published until the complete response is checked.
		for (std::size_t i = 0; i < parsed.count; ++i) {
			const auto& source = parsed.items[i];
			const client::ResultView item{client::Status::Ok,
			                              binding.entries_[i].operation,
			                              source.dispatch,
			                              source.endpointStatus,
			                              0,
			                              source.payload};
			if (item.succeeded() && source.payload.size() != binding.entries_[i].responseBytes) {
				result.valueStatus = ValueStatus::InvalidPayload;
				return result;
			}
		}
		// Invalidated schemas may already have released their metadata storage.
		if (admitted && !contractsMatch(owner, binding)) {
			result.valueStatus = ValueStatus::TypeMismatch;
			return result;
		}
		// Check every successful DTO before publishing the first runtime view.
		for (std::size_t i = 0; i < parsed.count; ++i) {
			const auto& source = parsed.items[i];
			const auto& entry = binding.entries_[i];
			const client::ResultView item{
			    client::Status::Ok, entry.operation, source.dispatch, source.endpointStatus, 0,
			    source.payload};
			if (admitted && item.succeeded() && entry.responseType != 0 &&
			    !owner.state_.schema.view(entry.responseType, source.payload)) {
				result.valueStatus = ValueStatus::InvalidPayload;
				return result;
			}
		}
		result.count = parsed.count;
		for (std::size_t i = 0; i < parsed.count; ++i) {
			const auto& source = parsed.items[i];
			auto& destination = result.items[i];
			destination.id = source.id;
			destination.operation = {client::Status::Ok,
			                         binding.entries_[i].operation,
			                         source.dispatch,
			                         source.endpointStatus,
			                         0,
			                         source.payload};
			destination.valueStatus = admitted ? ValueStatus::NoValue : ValueStatus::Stale;
		}
		if (!admitted)
			return result;
		result.valueStatus = ValueStatus::Ok;
		for (std::size_t i = 0; i < parsed.count; ++i) {
			auto& item = result.items[i];
			if (item.operation.succeeded() && binding.entries_[i].responseType != 0) {
				item.value = owner.state_.schema
				                 .view(binding.entries_[i].responseType, item.operation.payload)
				                 .value;
				item.valueStatus = ValueStatus::Ok;
			}
		}
		return result;
	}

	template<std::size_t N, std::size_t P, std::size_t C, class F>
	[[nodiscard]] static client::Submission
	submit(dynamic::DynamicClient<N, P, C>& owner, const BatchBinding& binding,
	       std::span<const client::Input> payloads, client::Output requestScratch, F&& callback,
	       std::uint32_t timeoutMs) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const BatchResult&> &&
		                  std::is_nothrow_constructible_v<std::decay_t<F>, F&&>,
		              "Batch completion ownership and invocation must be noexcept");
		const auto gate = owner.submissionGate();
		if (gate != client::Status::Ok)
			return {gate, {}};
		if (!current(owner, binding))
			return {client::Status::NotAdmitted, {}};
		if (!binding.supported_ || !binding.supported_(owner.backend_))
			return {client::Status::Unsupported, {}};
		if (!timeoutMs || timeoutMs > client::maxTimeoutMs || payloads.size() != binding.count_)
			return {client::Status::InvalidArgument, {}};
		if constexpr (std::is_pointer_v<std::decay_t<F>>)
			if (!callback)
				return {client::Status::InvalidArgument, {}};
		if (!contractsMatch(owner, binding))
			return {client::Status::InvalidArgument, {}};
		if (requestScratch.size() < binding.requestBytes_)
			return {client::Status::BufferTooSmall, {}};
		std::array<wire::RequestItem, wire::maxItems> items{};
		for (std::size_t i = 0; i < binding.count_; ++i) {
			const auto& entry = binding.entries_[i];
			if (payloads[i].size() != entry.requestBytes)
				return {client::Status::InvalidArgument, {}};
			if (entry.requestType != 0 && !owner.state_.schema.view(entry.requestType, payloads[i]))
				return {client::Status::CodecError, {}};
			items[i] = {static_cast<protocol::encoded::Operation>(entry.operation), entry.id,
			            payloads[i]};
		}
		const auto frozen = binding;
		const auto written = wire::writeRequest(
		    requestScratch, std::span{items}.first(frozen.count_), frozen.capability_.itemLimit);
		if (!written)
			return {client::Status::InvalidArgument, {}};
		dynamic::detail::CallbackScope publication{owner.callbackActive_};
		Completion<dynamic::DynamicClient<N, P, C>, std::decay_t<F>> completion{
		    &owner, frozen, std::forward<F>(callback)};
		static_assert(
		    sizeof(completion) <= C,
		    "Batch copies up to 32 runtime contracts; explicitly select CallbackBytes (1536 fits the binding and small callback)");
		const auto capturedGate = owner.submissionGate();
		if (capturedGate != client::Status::Ok)
			return {capturedGate, {}};
		if (!current(owner, completion.binding))
			return {client::Status::NotAdmitted, {}};
		// User callable ownership may run code. The caller keeps scratch and
		// request bytes stable through this whole submission call.
		return owner.core_.submitEncoded({client::Operation::Batch, 0,
		                                  client::Input{requestScratch}.first(written.written),
		                                  frozen.responseBytes_},
		                                 std::move(completion), timeoutMs);
	}
};
} // namespace detail

// Capability comes from the application's discovery path, never from TRM1.
// A concrete explicitly enabled Encoded backend is required. An erased backend
// or RPC returns Unsupported. Repeated operations/IDs retain their given order.
template<std::size_t N, std::size_t P, std::size_t C, class B>
    requires std::is_convertible_v<B*, client::Backend*>
[[nodiscard]] BatchBindingResult bindBatch(dynamic::DynamicClient<N, P, C>& owner, B& backend,
                                           wire::Capability capability,
                                           std::span<const Endpoint> endpoints) noexcept
{
	return detail::BatchAccess::bind(owner, backend, capability, endpoints);
}

// Scratch is caller-owned temporary wire storage, disjoint from payloads,
// their span descriptors, binding, callback, schema/client/backend storage.
// It remains stable through this call (including user callable moves). The
// existing FIFO copies the complete request before return; scratch and source
// payloads may then be reused. Rejected submission may change scratch only.
template<std::size_t N, std::size_t P, std::size_t C, class F>
[[nodiscard]] client::Submission
batch(dynamic::DynamicClient<N, P, C>& owner, const BatchBinding& binding,
      std::span<const client::Input> payloads, client::Output requestScratch, F&& callback,
      std::uint32_t timeoutMs = 30000) noexcept
{
	return detail::BatchAccess::submit(owner, binding, payloads, requestScratch,
	                                   std::forward<F>(callback), timeoutMs);
}
} // namespace telemetry::integration::batch
#endif // TELEMETRY_INTEGRATION_BATCH_CLIENT_HPP
