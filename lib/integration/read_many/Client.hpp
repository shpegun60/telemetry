/*
 * @file Client.hpp
 * @brief Optional scalar Encoded ReadMany over DynamicClient's existing FIFO.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_READ_MANY_CLIENT_HPP
#define TELEMETRY_INTEGRATION_READ_MANY_CLIENT_HPP
#pragma once

#include <integration/dynamic/DynamicClient.hpp>
#include <wire/ReadManyWire.hpp>

#include <concepts>

namespace telemetry::integration::read_many {
namespace wire = ::telemetry::protocol::encoded::read_many;
using ValueStatus = dynamic::ValueStatus;

namespace detail {
struct ReadManyAccess;
}

// Issued only by bindReadMany. IDs, types and the admitted capability are copied;
// source arrays need not remain alive. The issuing client and workspace object
// must outlive the binding, exactly as for DynamicClient::EndpointBinding.
class ReadManyBinding {
public:
	struct Entry {
		std::uint32_t id = 0;
		TypeId type = 0;
		std::uint8_t wireBytes = 0;
	};

	[[nodiscard]] std::span<const Entry> entries() const noexcept
	{
		return {entries_.data(), count_};
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
	std::uint32_t session_ = 0, responseBytes_ = 0;
	std::uint16_t count_ = 0;
	std::array<Entry, wire::maxItems> entries_{};
	friend struct detail::ReadManyAccess;
};

struct ReadManyBindingResult {
	client::Status status = client::Status::NotAdmitted;
	ReadManyBinding binding{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == client::Status::Ok;
	}
};

struct ReadManyItem {
	std::uint32_t id = 0;
	wire::ItemStatus status = wire::ItemStatus::ReadError;
	ValueStatus valueStatus = ValueStatus::NoValue;
	runtime::DynamicValueView value{};

	[[nodiscard]] bool hasValue() const noexcept
	{
		return valueStatus == ValueStatus::Ok && value.valid();
	}
};

// Views borrow the backend result until the callback returns. Item errors do not
// invalidate other items. A malformed/type-invalid body publishes no item views.
// The operation outcome is retained even when schema invalidation forbids views.
struct ReadManyResult {
	client::ResultView operation{};
	ValueStatus valueStatus = ValueStatus::Stale;
	std::uint16_t count = 0;
	std::array<ReadManyItem, wire::maxItems> items{};
};

namespace detail {
// This optional integration is the sole friend, keeping wire dependencies out
// of DynamicClient. It neither owns a client nor schedules work independently.
struct ReadManyAccess {
	template<class Client, class F>
	struct Completion {
		Client* owner;
		ReadManyBinding binding;
		F callable;

		void operator()(const client::ResultView& operation) noexcept
		{
			dynamic::detail::CallbackScope guard{owner->callbackActive_};
			const auto result = complete(*owner, binding, operation);
			callable(result);
		}
	};

	template<class Client>
	[[nodiscard]] static bool current(const Client& owner, const ReadManyBinding& binding) noexcept
	{
		return owner.isCurrent() && binding.client_ == &owner && binding.epoch_ == owner.epoch_ &&
		       binding.workspace_ == owner.state_.schema.owner() &&
		       binding.schemaGeneration_ == owner.state_.schema.generation() &&
		       binding.session_ == owner.state_.session;
	}

	template<class Client>
	[[nodiscard]] static bool contractsMatch(const Client& owner,
	                                         const ReadManyBinding& binding) noexcept
	{
		for (const auto& entry : binding.entries()) {
			const auto* endpoint = owner.find(descriptor::Category::Field, entry.id);
			const auto* type = owner.state_.schema.type(entry.type);
			if (!endpoint || endpoint->typeId != entry.type || !type ||
			    type->wireBytes != entry.wireBytes ||
			    owner.permissionFor(client::Operation::FieldRead, endpoint->capabilities) !=
			        client::Status::Ok)
				return false;
		}
		return true;
	}

	template<std::size_t N, std::size_t P, std::size_t C, class B>
	[[nodiscard]] static ReadManyBindingResult bind(dynamic::DynamicClient<N, P, C>& owner,
	                                                B& backend, wire::Capability capability,
	                                                std::span<const std::uint32_t> ids) noexcept
	{
		const auto gate = owner.submissionGate();
		if (gate != client::Status::Ok)
			return {gate, {}};
		if constexpr (!requires(const B& value) {
			              { value.supportsReadMany() } noexcept -> std::same_as<bool>;
		              }) {
			return {client::Status::Unsupported, {}};
		} else {
			if (static_cast<const client::Backend*>(&backend) != &owner.backend_ ||
			    !backend.supportsReadMany())
				return {client::Status::Unsupported, {}};
			if (!wire::valid(capability))
				return {client::Status::InvalidArgument, {}};
			if (capability.session != owner.state_.session ||
			    capability.fingerprint != owner.state_.schema.fingerprint())
				return {client::Status::NotAdmitted, {}};
			if (ids.empty() || ids.size() > capability.itemLimit)
				return {client::Status::InvalidArgument, {}};
			if (2 + 4 * ids.size() > capability.requestLimit)
				return {client::Status::BufferTooSmall, {}};
			ReadManyBinding binding;
			binding.client_ = &owner;
			binding.workspace_ = owner.state_.schema.owner();
			binding.epoch_ = owner.epoch_;
			binding.schemaGeneration_ = owner.state_.schema.generation();
			binding.session_ = capability.session;
			binding.capability_ = capability;
			binding.count_ = static_cast<std::uint16_t>(ids.size());
			binding.supported_ = +[](const client::Backend& value) noexcept {
				return static_cast<const B&>(value).supportsReadMany();
			};
			std::uint32_t responseBytes = 2;
			for (std::size_t i = 0; i < ids.size(); ++i) {
				for (std::size_t j = 0; j < i; ++j)
					if (ids[j] == ids[i])
						return {client::Status::InvalidArgument, {}};
				const auto* endpoint = owner.find(descriptor::Category::Field, ids[i]);
				if (!endpoint || owner.permissionFor(client::Operation::FieldRead,
				                                     endpoint->capabilities) != client::Status::Ok)
					return {client::Status::InvalidArgument, {}};
				const auto* type = owner.state_.schema.type(endpoint->typeId);
				if (!type || (type->kind != TypeKind::Scalar && type->kind != TypeKind::Enum) ||
				    (type->wireBytes != 1 && type->wireBytes != 2 && type->wireBytes != 4 &&
				     type->wireBytes != 8))
					return {client::Status::Unsupported, {}};
				binding.entries_[i] = {ids[i], type->id,
				                       static_cast<std::uint8_t>(type->wireBytes)};
				responseBytes += 8 + type->wireBytes;
			}
			if (responseBytes > capability.replyLimit)
				return {client::Status::BufferTooSmall, {}};
			binding.responseBytes_ = responseBytes;
			return {client::Status::Ok, binding};
		}
	}

	template<class Client>
	[[nodiscard]] static ReadManyResult complete(const Client& owner,
	                                             const ReadManyBinding& binding,
	                                             const client::ResultView& operation) noexcept
	{
		ReadManyResult result{operation};
		if (!current(owner, binding))
			return result;
		result.valueStatus = ValueStatus::NoValue;
		if (!operation.succeeded())
			return result;
		if (operation.operation != client::Operation::ReadMany || !contractsMatch(owner, binding)) {
			result.valueStatus = ValueStatus::TypeMismatch;
			return result;
		}
		std::array<std::uint32_t, wire::maxItems> ids{};
		for (std::size_t i = 0; i < binding.count_; ++i)
			ids[i] = binding.entries_[i].id;
		const auto parsed =
		    wire::parseReply(operation.payload, std::span{ids}.first(binding.count_));
		result.valueStatus = ValueStatus::InvalidPayload;
		if (!parsed)
			return result;
		// Validate every successful scalar before publishing any view. Failed
		// items carry their wire status and no value, without poisoning siblings.
		for (std::size_t i = 0; i < parsed.count; ++i) {
			const auto& item = parsed.items[i];
			if (item.status == wire::ItemStatus::Ok &&
			    (item.payload.size() != binding.entries_[i].wireBytes ||
			     !owner.state_.schema.view(binding.entries_[i].type, item.payload)))
				return result;
		}
		result.valueStatus = ValueStatus::Ok;
		result.count = parsed.count;
		for (std::size_t i = 0; i < parsed.count; ++i) {
			const auto& item = parsed.items[i];
			auto& destination = result.items[i];
			destination.id = item.id;
			destination.status = item.status;
			if (item.status == wire::ItemStatus::Ok) {
				destination.valueStatus = ValueStatus::Ok;
				destination.value =
				    owner.state_.schema.view(binding.entries_[i].type, item.payload).value;
			}
		}
		return result;
	}

	template<std::size_t N, std::size_t P, std::size_t C, class F>
	[[nodiscard]] static client::Submission submit(dynamic::DynamicClient<N, P, C>& owner,
	                                               const ReadManyBinding& binding, F&& callback,
	                                               std::uint32_t timeoutMs) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const ReadManyResult&>,
		              "ReadMany completion must be noexcept");
		static_assert(std::is_nothrow_constructible_v<std::decay_t<F>, F&&>,
		              "ReadMany completion ownership must not throw");
		const auto gate = owner.submissionGate();
		if (gate != client::Status::Ok)
			return {gate, {}};
		if (!current(owner, binding))
			return {client::Status::NotAdmitted, {}};
		if (!binding.supported_ || !binding.supported_(owner.backend_))
			return {client::Status::Unsupported, {}};
		if (!contractsMatch(owner, binding))
			return {client::Status::InvalidArgument, {}};
		std::array<std::uint32_t, wire::maxItems> ids{};
		for (std::size_t i = 0; i < binding.count_; ++i)
			ids[i] = binding.entries_[i].id;
		std::array<std::byte, wire::maxRequestPayload> payload{};
		const auto written = wire::writeRequest(payload, std::span{ids}.first(binding.count_),
		                                        binding.capability_.itemLimit);
		if (!written)
			return {client::Status::InvalidArgument, {}};
		dynamic::detail::CallbackScope guard{owner.callbackActive_};
		Completion<dynamic::DynamicClient<N, P, C>, std::decay_t<F>> completion{
		    &owner, binding, std::forward<F>(callback)};
		static_assert(sizeof(completion) <= C,
		              "ReadMany captures up to 32 IDs/types by value; select DynamicClient's "
		              "CallbackBytes explicitly (640 bytes fits the binding and a small callback)");
		// A user move can invalidate or close admission. It cannot replace the
		// session while guarded, and a revoked binding never reaches the FIFO.
		const auto capturedGate = owner.submissionGate();
		if (capturedGate != client::Status::Ok)
			return {capturedGate, {}};
		if (!current(owner, completion.binding))
			return {client::Status::NotAdmitted, {}};
		return owner.core_.submitEncoded({client::Operation::ReadMany, 0,
		                                  client::Input{payload}.first(written.written),
		                                  completion.binding.responseBytes_},
		                                 std::move(completion), timeoutMs);
	}
};
} // namespace detail

// The application obtains capability from its trusted discovery path and keeps
// it immutable for this admitted schema/session. Supplying a capability is not
// peer discovery or authentication. A concrete enabled Encoded backend is
// required; an erased Backend& and RPC explicitly return Unsupported.
template<std::size_t N, std::size_t P, std::size_t C, class B>
    requires std::is_convertible_v<B*, client::Backend*>
[[nodiscard]] ReadManyBindingResult bindReadMany(dynamic::DynamicClient<N, P, C>& owner, B& backend,
                                                 wire::Capability capability,
                                                 std::span<const std::uint32_t> ids) noexcept
{
	return detail::ReadManyAccess::bind(owner, backend, capability, ids);
}

template<std::size_t N, std::size_t P, std::size_t C, class F>
[[nodiscard]] client::Submission readMany(dynamic::DynamicClient<N, P, C>& owner,
                                          const ReadManyBinding& binding, F&& callback,
                                          std::uint32_t timeoutMs = 30000) noexcept
{
	return detail::ReadManyAccess::submit(owner, binding, std::forward<F>(callback), timeoutMs);
}
} // namespace telemetry::integration::read_many
#endif // TELEMETRY_INTEGRATION_READ_MANY_CLIENT_HPP
