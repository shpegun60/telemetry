/*
 * @file InspectorModel.hpp
 * @brief Fixed-storage, owner-only inspection of runtime fields and copied values.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_INSPECTOR_INSPECTOR_MODEL_HPP
#define TELEMETRY_INTEGRATION_INSPECTOR_INSPECTOR_MODEL_HPP
#pragma once

#include <integration/dynamic/DynamicClient.hpp>
#include <integration/inspector/SnapshotStore.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace telemetry::integration::workbench {
template<std::size_t, std::size_t, std::size_t, std::size_t>
class WorkbenchModel;
}

namespace telemetry::integration::inspector {

// These fields are copied, with no borrowed ResultView::payload retained.
struct OperationResult {
	client::Status status = client::Status::Ok;
	client::Operation operation = client::Operation::FieldRead;
	std::uint8_t dispatch = 0, endpointStatus = 0, codec = 0;

	[[nodiscard]] bool succeeded() const noexcept
	{
		return status == client::Status::Ok && dispatch == 0 &&
		       (endpointStatus == 0 ||
		        (operation == client::Operation::Command && endpointStatus == 1));
	}

	[[nodiscard]] bool accepted() const noexcept
	{
		return status == client::Status::Ok && dispatch == 0 &&
		       operation == client::Operation::Command && endpointStatus == 1;
	}

	[[nodiscard]] bool executed() const noexcept
	{
		return status == client::Status::Ok && dispatch == 0 &&
		       operation == client::Operation::Command && endpointStatus == 0;
	}
};

struct ReadResult {
	OperationResult operation{};
	dynamic::ValueStatus valueStatus = dynamic::ValueStatus::Stale;
	ModelStatus modelStatus = ModelStatus::Stale;
	std::uint32_t fieldId = 0;
	std::uint64_t revision = 0;
	bool published = false;
};

struct ReadSubmission {
	client::Status status = client::Status::InvalidArgument;
	ModelStatus modelStatus = ModelStatus::Ok;
	client::RequestHandle handle{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == client::Status::Ok && modelStatus == ModelStatus::Ok;
	}
};

template<std::size_t N = 8, std::size_t PayloadBytes = 4096, std::size_t SnapshotBytes = 4096,
         std::size_t CallbackBytes = 256>
class InspectorModel;

// The application supplies a fully parsed schema, observed header and transport.
// Descriptor/index storage and the workspace control object retain RuntimeSchema's
// borrowed lifetime contract. No Descriptor fetching, second FIFO, allocation,
// mutation API or raw DynamicValueView/payload accessor is supplied here.
// One owner serializes all calls, callbacks and schema reset/reparse. Destruction
// requires close/process/drained plus actual port/peer release, as for DynamicClient.
// CallbackBytes includes DynamicClient's binding and this model's revision capture.
template<std::size_t N, std::size_t PayloadBytes, std::size_t SnapshotBytes,
         std::size_t CallbackBytes>
class InspectorModel {
public:
	explicit InspectorModel(client::Backend& backend, client::Clock clock) noexcept
	    : client_(backend, clock),
	      snapshot_(storage_, {this, +[](const void* context) noexcept {
		                           const auto& self = *static_cast<const InspectorModel*>(context);
		                           return self.client_.isCurrent();
	                           }})
	{}

	InspectorModel(const InspectorModel&) = delete;
	InspectorModel& operator=(const InspectorModel&) = delete;
	InspectorModel(InspectorModel&&) = delete;
	InspectorModel& operator=(InspectorModel&&) = delete;

	[[nodiscard]] client::Status admit(const runtime::RuntimeSchema& schema,
	                                   client::Input observedHeader, std::uint32_t session) noexcept
	{
		if (closed_)
			return client::Status::Closed;
		if (client_.recoveryRequired())
			return client::Status::RecoveryRequired;
		if (publishing_)
			return client::Status::Busy;
		if (!snapshot_.canAdmit())
			return client::Status::IdsExhausted;
		const auto status = client_.admit(schema, observedHeader, session);
		if (status != client::Status::Ok)
			return status;
		if (snapshot_.admit(schema, session) != ModelStatus::Ok) {
			client_.invalidate();
			return client::Status::InvalidArgument;
		}
		return client::Status::Ok;
	}

	[[nodiscard]] std::span<const runtime::RuntimeCatalog>
	catalogs(descriptor::Category category) const noexcept
	{
		return current() ? schema().catalogs(category) : std::span<const runtime::RuntimeCatalog>{};
	}

	[[nodiscard]] std::span<const runtime::RuntimeEndpoint>
	endpoints(descriptor::Category category) const noexcept
	{
		return current() ? schema().endpoints(category)
		                 : std::span<const runtime::RuntimeEndpoint>{};
	}

	[[nodiscard]] auto fields() const noexcept
	{
		return endpoints(descriptor::Category::Field);
	}

	[[nodiscard]] auto commands() const noexcept
	{
		return endpoints(descriptor::Category::Command);
	}

	[[nodiscard]] auto services() const noexcept
	{
		return endpoints(descriptor::Category::Service);
	}

	[[nodiscard]] dynamic::EndpointResult bindField(std::uint32_t id) const noexcept
	{
		const auto gate = submissionGate();
		return gate == client::Status::Ok ? client_.bind(client::Operation::FieldRead, id)
		                                  : dynamic::EndpointResult{gate, {}};
	}

	// Explicit selection clears the snapshot. Rejected selection leaves it intact.
	[[nodiscard]] client::Status selectField(const dynamic::EndpointBinding& field) noexcept
	{
		const auto status = validateField(field);
		if (status != client::Status::Ok)
			return status;
		if (snapshot_.select(field.id(), field.responseType()) != ModelStatus::Ok)
			return client::Status::IdsExhausted;
		selection_ = field;
		return client::Status::Ok;
	}

	void clearSelection() noexcept
	{
		if (!publishing_)
			snapshot_.clearSelection();
	}

	// Submission refusal is transactional: selection and any published snapshot
	// remain intact. Only an accepted read starts a new revision. The underlying
	// FIFO never invokes its callback synchronously during publication.
	template<class F>
	[[nodiscard]] ReadSubmission readField(const dynamic::EndpointBinding& field, F&& callback,
	                                       std::uint32_t timeoutMs = 30000) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const ReadResult&>,
		              "Inspector completion must be noexcept");
		static_assert(std::is_nothrow_constructible_v<std::decay_t<F>, F&&>,
		              "Inspector completion ownership must not throw");
		const auto gate = validateField(field);
		if (gate != client::Status::Ok)
			return {gate, ModelStatus::Ok, {}};
		if constexpr (std::is_pointer_v<std::decay_t<F>>)
			if (!callback)
				return {client::Status::InvalidArgument, ModelStatus::Ok, {}};
		if (field.responseBytes() > snapshot_.capacity())
			return {client::Status::BufferTooSmall, ModelStatus::SnapshotTooLarge, {}};
		if (!snapshot_.canAdvance())
			return {client::Status::IdsExhausted, ModelStatus::RevisionExhausted, {}};
		const auto revision = snapshot_.revision() + 1;
		const auto admission = snapshot_.admission();
		// Owning/moving/destroying the callable can run user code. Freeze the
		// binding and protect this predicted revision until publication finishes.
		const auto frozen = field;
		const auto id = frozen.id();
		dynamic::detail::CallbackScope publication{publishing_};
		const auto submitted = client_.readField(
		    frozen,
		    [this, admission, revision, id, fn = std::forward<F>(callback)](
		        const dynamic::DynamicResult& result) mutable noexcept {
			    const auto completed = publish(result, admission, revision, id);
			    fn(completed);
		    },
		    timeoutMs);
		if (!submitted)
			return {submitted.status, ModelStatus::Ok, {}};
		(void)snapshot_.select(frozen.id(), frozen.responseType());
		selection_ = frozen;
		return {client::Status::Ok, ModelStatus::Ok, submitted.handle};
	}

	template<class F>
	[[nodiscard]] ReadSubmission refresh(F&& callback, std::uint32_t timeoutMs = 30000) noexcept
	{
		const auto gate = submissionGate();
		if (gate != client::Status::Ok)
			return {gate, ModelStatus::Ok, {}};
		if (!snapshot_.snapshotInfo().selected)
			return {client::Status::InvalidArgument, ModelStatus::NoSelection, {}};
		return readField(selection_, std::forward<F>(callback), timeoutMs);
	}

	[[nodiscard]] SnapshotInfo snapshotInfo() const noexcept
	{
		return current() ? snapshot_.snapshotInfo() : SnapshotInfo{};
	}

	[[nodiscard]] NodeResult root() const noexcept
	{
		return current() ? snapshot_.root() : NodeResult{ModelStatus::Stale, {}};
	}

	[[nodiscard]] Result<NodeInfo> info(const NodeHandle& node) const noexcept
	{
		return snapshot_.info(node);
	}

	[[nodiscard]] Result<std::uint32_t> childCount(const NodeHandle& node) const noexcept
	{
		return snapshot_.childCount(node);
	}

	[[nodiscard]] NodeResult child(const NodeHandle& parent, std::uint32_t index) const noexcept
	{
		return snapshot_.child(parent, index);
	}

	[[nodiscard]] Result<ScalarValue> scalar(const NodeHandle& node) const noexcept
	{
		return snapshot_.scalar(node);
	}

	[[nodiscard]] Result<std::size_t> decimal(const NodeHandle& node,
	                                          std::span<char> output) const noexcept
	{
		return snapshot_.decimal(node, output);
	}

	[[nodiscard]] bool isCurrent() const noexcept
	{
		return current();
	}

	[[nodiscard]] bool isCurrent(const dynamic::EndpointBinding& field) const noexcept
	{
		return current() && client_.isCurrent(field);
	}

	void invalidate() noexcept
	{
		snapshot_.invalidate();
		client_.invalidate();
	}

	[[nodiscard]] bool confirmQuiescence(dynamic::Quiescence proof) noexcept
	{
		if (publishing_)
			return false;
		const bool confirmed = client_.confirmQuiescence(proof);
		if (confirmed) {
			snapshot_.invalidate();
		}
		return confirmed;
	}

	void process(std::uint32_t now) noexcept
	{
		if (publishing_)
			return;
		(void)current();
		client_.process(now);
		(void)current();
	}

	[[nodiscard]] client::Packet nextPacket() noexcept
	{
		if (publishing_)
			return {};
		(void)current();
		return client_.nextPacket();
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t session, std::uint32_t requestId) noexcept
	{
		if (publishing_)
			return false;
		(void)current();
		return client_.prepareHandoff(session, requestId);
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t session, std::uint32_t requestId) noexcept
	{
		return !publishing_ && client_.onSendAccepted(session, requestId);
	}

	[[nodiscard]] bool onReply(client::Input bytes, std::uint32_t session,
	                           std::uint32_t requestId) noexcept
	{
		return !publishing_ && client_.onReply(bytes, session, requestId);
	}

	[[nodiscard]] bool cancel(client::RequestHandle handle) noexcept
	{
		return !publishing_ && client_.cancel(handle);
	}

	[[nodiscard]] std::size_t pending() const noexcept
	{
		return client_.pending();
	}

	[[nodiscard]] bool drained() const noexcept
	{
		return !publishing_ && client_.drained();
	}

	[[nodiscard]] bool recoveryRequired() const noexcept
	{
		return client_.recoveryRequired();
	}

	[[nodiscard]] std::uint32_t session() const noexcept
	{
		return client_.session();
	}

	void close() noexcept
	{
		closed_ = true;
		snapshot_.invalidate();
		client_.close();
	}

private:
	template<std::size_t, std::size_t, std::size_t, std::size_t>
	friend class ::telemetry::integration::workbench::WorkbenchModel;

	dynamic::DynamicClient<N, PayloadBytes, CallbackBytes> client_;
	std::array<std::byte, SnapshotBytes> storage_{};
	SnapshotStore snapshot_;
	dynamic::EndpointBinding selection_{};
	bool closed_ = false, publishing_ = false;

	[[nodiscard]] const runtime::RuntimeSchema& schema() const noexcept
	{
		return snapshot_.schema_;
	}

	[[nodiscard]] bool current() const noexcept
	{
		return snapshot_.isCurrent();
	}

	[[nodiscard]] client::Status submissionGate() const noexcept
	{
		if (closed_)
			return client::Status::Closed;
		if (client_.recoveryRequired())
			return client::Status::RecoveryRequired;
		if (!current())
			return client::Status::NotAdmitted;
		return publishing_ ? client::Status::Busy : client::Status::Ok;
	}

	[[nodiscard]] client::Status validateField(const dynamic::EndpointBinding& field) const noexcept
	{
		const auto gate = submissionGate();
		if (gate != client::Status::Ok)
			return gate;
		if (!client_.isCurrent(field))
			return client::Status::NotAdmitted;
		if (field.operation() != client::Operation::FieldRead)
			return client::Status::InvalidArgument;
		return client_.bind(client::Operation::FieldRead, field.id()).status;
	}

	[[nodiscard]] ReadResult publish(const dynamic::DynamicResult& result, std::uint64_t admission,
	                                 std::uint64_t revision, std::uint32_t id) noexcept
	{
		const auto& operation = result.operation;
		ReadResult completed{{operation.status, operation.operation, operation.dispatch,
		                      operation.endpointStatus, operation.codec},
		                     result.valueStatus,
		                     ModelStatus::Stale,
		                     id,
		                     revision,
		                     false};
		if (admission != snapshot_.admission() || revision != snapshot_.revision() || !current() ||
		    !snapshot_.snapshotInfo().selected || !client_.isCurrent(selection_))
			return completed;
		if (!result.hasValue()) {
			completed.modelStatus = result.valueStatus == dynamic::ValueStatus::InvalidPayload
			                            ? ModelStatus::InvalidPayload
			                        : result.valueStatus == dynamic::ValueStatus::TypeMismatch
			                            ? ModelStatus::TypeMismatch
			                            : ModelStatus::NoSnapshot;
			return completed;
		}
		completed.modelStatus = snapshot_.publishValidated(operation.payload, admission, revision);
		completed.published = completed.modelStatus == ModelStatus::Ok;
		return completed;
	}
};

} // namespace telemetry::integration::inspector
#endif // TELEMETRY_INTEGRATION_INSPECTOR_INSPECTOR_MODEL_HPP
