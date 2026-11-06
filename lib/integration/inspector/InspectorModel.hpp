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
template<std::size_t, std::size_t, std::size_t, std::size_t>
class WorkbenchView;

namespace detail {
template<std::size_t, std::size_t, std::size_t, std::size_t, bool>
class WorkbenchState;
}
} // namespace telemetry::integration::workbench

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

namespace detail {
// The owned specialization is the sole client/FIFO. Borrowed state stores only
// its address; no inactive client, queue or transport buffer is constructed.
template<std::size_t N, std::size_t P, std::size_t C, bool Borrowed>
struct ClientStorage;

template<std::size_t N, std::size_t P, std::size_t C>
struct ClientStorage<N, P, C, false> {
	dynamic::DynamicClient<N, P, C> value;

	ClientStorage(client::Backend& backend, client::Clock clock) noexcept : value(backend, clock)
	{}

	auto& get() noexcept
	{
		return value;
	}

	const auto& get() const noexcept
	{
		return value;
	}
};

template<std::size_t N, std::size_t P, std::size_t C>
struct ClientStorage<N, P, C, true> {
	dynamic::DynamicClient<N, P, C>* value;

	explicit ClientStorage(dynamic::DynamicClient<N, P, C>& owner) noexcept : value(&owner)
	{}

	auto& get() noexcept
	{
		return *value;
	}

	const auto& get() const noexcept
	{
		return *value;
	}
};

template<std::size_t, std::size_t, std::size_t, std::size_t, bool>
class InspectorState;

template<std::size_t N, std::size_t P, std::size_t S, std::size_t C, bool Borrowed>
struct OwnerLink {};

template<std::size_t N, std::size_t P, std::size_t S, std::size_t C>
struct OwnerLink<N, P, S, C, true> {
	InspectorState<N, P, S, C, false>* owner = nullptr;
	std::uint64_t admission = 0;
};

// The algorithms below are shared by the owning Inspector and borrowed views.
// Its internal lifecycle methods are deliberately not exported by WorkbenchView.
// The application supplies a fully parsed schema, observed header and transport.
// Descriptor/index storage and the workspace control object retain RuntimeSchema's
// borrowed lifetime contract. No Descriptor fetching, second FIFO, allocation,
// mutation API or raw DynamicValueView/payload accessor is supplied here.
// One owner serializes all calls, callbacks and schema reset/reparse. Destruction
// requires close/process/drained plus actual port/peer release, as for DynamicClient.
// CallbackBytes includes DynamicClient's binding and this model's revision capture.
template<std::size_t N, std::size_t PayloadBytes, std::size_t SnapshotBytes,
         std::size_t CallbackBytes, bool Borrowed>
class InspectorState {
public:
	explicit InspectorState(client::Backend& backend, client::Clock clock) noexcept
	    requires(!Borrowed)
	    : clientStorage_(backend, clock),
	      snapshot_(storage_, {this, +[](const void* context) noexcept {
		                           const auto& self = *static_cast<const InspectorState*>(context);
		                           return self.admissionCurrent();
	                           }})
	{}

	explicit InspectorState(
	    InspectorState<N, PayloadBytes, SnapshotBytes, CallbackBytes, false>& owner) noexcept
	    requires Borrowed
	    : clientStorage_(owner.client()), ownerLink_{&owner, 0},
	      snapshot_(storage_,
	                {this, +[](const void* context) noexcept {
		                 return static_cast<const InspectorState*>(context)->admissionCurrent();
	                 }})
	{}

	[[nodiscard]] client::Status synchronize() noexcept
	    requires Borrowed
	{
		if (closed_)
			return client::Status::Closed;
		if (client().recoveryRequired())
			return client::Status::RecoveryRequired;
		if (publishing_ || ownerPublishing() || callbacks_)
			return client::Status::Busy;
		if (!ownerLink_.owner->isCurrent())
			return client::Status::NotAdmitted;
		if (current())
			return client::Status::Ok;
		if (!snapshot_.canAdmit())
			return client::Status::IdsExhausted;
		ownerLink_.admission = ownerLink_.owner->snapshot_.admission();
		const auto status =
		    snapshot_.admit(ownerLink_.owner->schema(), ownerLink_.owner->session());
		return status == ModelStatus::Ok ? client::Status::Ok : client::Status::InvalidArgument;
	}

	void invalidateData() noexcept
	{
		snapshot_.retire();
	}

	void retire() noexcept
	    requires Borrowed
	{
		closed_ = true;
		snapshot_.invalidate();
	}

	InspectorState(const InspectorState&) = delete;
	InspectorState& operator=(const InspectorState&) = delete;
	InspectorState(InspectorState&&) = delete;
	InspectorState& operator=(InspectorState&&) = delete;

	[[nodiscard]] client::Status admit(const runtime::RuntimeSchema& schema,
	                                   client::Input observedHeader, std::uint32_t session) noexcept
	{
		if (closed_)
			return client::Status::Closed;
		if (client().recoveryRequired())
			return client::Status::RecoveryRequired;
		if (publishing_ || transportPublishing_)
			return client::Status::Busy;
		if (!snapshot_.canAdmit())
			return client::Status::IdsExhausted;
		const auto status = client().admit(schema, observedHeader, session);
		if (status != client::Status::Ok)
			return status;
		if (snapshot_.admit(schema, session) != ModelStatus::Ok) {
			client().invalidate();
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
		return gate == client::Status::Ok ? client().bind(client::Operation::FieldRead, id)
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
		if constexpr (std::is_pointer_v<std::remove_reference_t<F>>)
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
		dynamic::detail::CallbackScope ownerPublication{ownerPublishing()};
		const auto submitted = client().readField(
		    frozen,
		    holdCallback([this, admission, revision, id, fn = std::forward<F>(callback)](
		                     const dynamic::DynamicResult& result) mutable noexcept {
			    const auto completed = publish(result, admission, revision, id);
			    fn(completed);
		    }),
		    timeoutMs);
		if (!submitted)
			return {submitted.status, ModelStatus::Ok, {}};
		if (admissionGate() != client::Status::Ok) {
			(void)client().cancel(submitted.handle);
			return {client::Status::Ok, ModelStatus::Ok, submitted.handle};
		}
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
		return current() && client().isCurrent(field);
	}

	void invalidate() noexcept
	{
		snapshot_.invalidate();
		if constexpr (!Borrowed)
			client().invalidate();
	}

	[[nodiscard]] bool confirmQuiescence(dynamic::Quiescence proof) noexcept
	{
		if (publishing_ || transportPublishing_)
			return false;
		const bool confirmed = client().confirmQuiescence(proof);
		if (confirmed) {
			snapshot_.invalidate();
		}
		return confirmed;
	}

	void process(std::uint32_t now) noexcept
	{
		if (publishing_ || transportPublishing_)
			return;
		(void)current();
		client().process(now);
		(void)current();
	}

	[[nodiscard]] client::Packet nextPacket() noexcept
	{
		if (publishing_ || transportPublishing_)
			return {};
		(void)current();
		return client().nextPacket();
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t session, std::uint32_t requestId) noexcept
	{
		if (publishing_ || transportPublishing_)
			return false;
		(void)current();
		return client().prepareHandoff(session, requestId);
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t session, std::uint32_t requestId) noexcept
	{
		return !publishing_ && !transportPublishing_ && client().onSendAccepted(session, requestId);
	}

	[[nodiscard]] bool onReply(client::Input bytes, std::uint32_t session,
	                           std::uint32_t requestId) noexcept
	{
		return !publishing_ && !transportPublishing_ && client().onReply(bytes, session, requestId);
	}

	[[nodiscard]] bool cancel(client::RequestHandle handle) noexcept
	{
		return !publishing_ && client().cancel(handle);
	}

	[[nodiscard]] std::size_t pending() const noexcept
	{
		return client().pending();
	}

	[[nodiscard]] bool drained() const noexcept
	{
		return !publishing_ && !transportPublishing_ && callbacks_ == 0 &&
		       (Borrowed || client().drained());
	}

	[[nodiscard]] bool recoveryRequired() const noexcept
	{
		return client().recoveryRequired();
	}

	[[nodiscard]] std::uint32_t session() const noexcept
	{
		return client().session();
	}

	void close() noexcept
	{
		closed_ = true;
		snapshot_.invalidate();
		if constexpr (!Borrowed)
			client().close();
	}

private:
	template<std::size_t, std::size_t, std::size_t, std::size_t>
	friend class ::telemetry::integration::workbench::WorkbenchModel;
	template<std::size_t, std::size_t, std::size_t, std::size_t, bool>
	friend class ::telemetry::integration::workbench::detail::WorkbenchState;
	template<std::size_t, std::size_t, std::size_t, std::size_t, bool>
	friend class InspectorState;

	ClientStorage<N, PayloadBytes, CallbackBytes, Borrowed> clientStorage_;

	[[nodiscard]] auto& client() noexcept
	{
		return clientStorage_.get();
	}

	[[nodiscard]] const auto& client() const noexcept
	{
		return clientStorage_.get();
	}

	// A lease remains until the FIFO has destroyed the callback, including the
	// user's captured callable. drained() is false during moves/destruction too.
	struct CallbackLease {
		std::size_t* count = nullptr;

		explicit CallbackLease(std::size_t& value) noexcept : count(&value)
		{
			++*count;
		}

		CallbackLease(CallbackLease&& other) noexcept : count(std::exchange(other.count, nullptr))
		{}

		CallbackLease(const CallbackLease&) = delete;
		CallbackLease& operator=(const CallbackLease&) = delete;

		~CallbackLease()
		{
			if (count)
				--*count;
		}
	};

	[[nodiscard]] CallbackLease callbackLease() noexcept
	{
		return CallbackLease{callbacks_};
	}

	// Preserve the exact existing owning callback layout and fit budget. Only
	// borrowed callbacks require an additional local lifetime lease. Forwarding
	// the owning temporary preserves its full-expression lifetime without a move.
	template<class F>
	[[nodiscard]] decltype(auto) holdCallback(F&& callback) noexcept
	{
		if constexpr (Borrowed) {
			return [lease = callbackLease(), fn = std::forward<F>(callback)](
			           const dynamic::DynamicResult& result) mutable noexcept {
				fn(result);
			};
		} else {
			return std::forward<F>(callback);
		}
	}

	std::size_t callbacks_ = 0;
	[[no_unique_address]] OwnerLink<N, PayloadBytes, SnapshotBytes, CallbackBytes, Borrowed>
	    ownerLink_{};

	[[nodiscard]] bool admissionCurrent() const noexcept
	{
		if constexpr (Borrowed)
			return !closed_ && ownerLink_.owner && ownerLink_.owner->isCurrent() &&
			       ownerLink_.admission == ownerLink_.owner->snapshot_.admission() &&
			       client().isCurrent();
		return client().isCurrent();
	}

	std::array<std::byte, SnapshotBytes> storage_{};
	SnapshotStore snapshot_;
	dynamic::EndpointBinding selection_{};
	bool closed_ = false, publishing_ = false, transportPublishing_ = false;

	[[nodiscard]] const runtime::RuntimeSchema& schema() const noexcept
	{
		return snapshot_.schema_;
	}

	[[nodiscard]] bool current() const noexcept
	{
		return snapshot_.isCurrent();
	}

	[[nodiscard]] client::Status admissionGate() const noexcept
	{
		if constexpr (Borrowed)
			if (ownerLink_.owner->closed_)
				return client::Status::Closed;
		if (closed_)
			return client::Status::Closed;
		if (client().recoveryRequired())
			return client::Status::RecoveryRequired;
		if (!current())
			return client::Status::NotAdmitted;
		return client::Status::Ok;
	}

	[[nodiscard]] bool& ownerPublishing() noexcept
	{
		if constexpr (Borrowed)
			return ownerLink_.owner->transportPublishing_;
		return transportPublishing_;
	}

	[[nodiscard]] bool ownerPublishing() const noexcept
	{
		if constexpr (Borrowed)
			return ownerLink_.owner->transportPublishing_;
		return transportPublishing_;
	}

	[[nodiscard]] client::Status submissionGate() const noexcept
	{
		const auto status = admissionGate();
		if (status != client::Status::Ok)
			return status;
		return publishing_ ? client::Status::Busy : client::Status::Ok;
	}

	[[nodiscard]] client::Status validateField(const dynamic::EndpointBinding& field) const noexcept
	{
		const auto gate = submissionGate();
		if (gate != client::Status::Ok)
			return gate;
		if (!client().isCurrent(field))
			return client::Status::NotAdmitted;
		if (field.operation() != client::Operation::FieldRead)
			return client::Status::InvalidArgument;
		return client().bind(client::Operation::FieldRead, field.id()).status;
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
		    !snapshot_.snapshotInfo().selected || !client().isCurrent(selection_)) {
			if constexpr (Borrowed)
				if (completed.valueStatus != dynamic::ValueStatus::NoValue)
					completed.valueStatus = dynamic::ValueStatus::Stale;
			return completed;
		}
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

} // namespace detail

// The existing owning API keeps its four parameters and transport lifecycle.
template<std::size_t N, std::size_t PayloadBytes, std::size_t SnapshotBytes,
         std::size_t CallbackBytes>
class InspectorModel
    : public detail::InspectorState<N, PayloadBytes, SnapshotBytes, CallbackBytes, false> {
	using Base = detail::InspectorState<N, PayloadBytes, SnapshotBytes, CallbackBytes, false>;

public:
	using Base::Base;
};

} // namespace telemetry::integration::inspector
#endif // TELEMETRY_INTEGRATION_INSPECTOR_INSPECTOR_MODEL_HPP
