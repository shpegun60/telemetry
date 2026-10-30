/*
 * @file WorkbenchModel.hpp
 * @brief Fixed-storage FieldWrite editing over the Inspector's single runtime client.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_WORKBENCH_WORKBENCH_MODEL_HPP
#define TELEMETRY_INTEGRATION_WORKBENCH_WORKBENCH_MODEL_HPP
#pragma once

#include <integration/inspector/InspectorModel.hpp>
#include <telemetry/runtime/DynamicValueWriter.hpp>
#include <common/DelegateSlot.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace telemetry::integration::workbench {

enum class Phase : std::uint8_t {
	Idle,
	Editing,
	Writing,
	Refreshing
};

// These independent outcomes own no response bytes or borrowed views.
struct ApplyResult {
	inspector::OperationResult write{client::Status::Ok, client::Operation::FieldWrite};
	bool writeCompleted = false;
	bool refreshAttempted = false;
	client::Status refreshSubmission = client::Status::InvalidArgument;
	bool refreshSubmitted = false;
	inspector::ReadResult refresh{};
	bool refreshCompleted = false;
};

// One owner serializes the facade, callbacks and schema changes. The private
// Inspector owns the only DynamicClient/FIFO. Draft and committed request bytes
// are separate from its published snapshot. No automatic mutation retry exists.
// A completion callback may reenter read/edit APIs; another apply while that
// callback is executing returns Busy. Destruction requires close/process/drained
// and actual transport release, and must wait until callbacks have returned.
template<std::size_t N = 8, std::size_t PayloadBytes = 4096, std::size_t SnapshotBytes = 4096,
         std::size_t CallbackBytes = 256>
class WorkbenchModel {
public:
	explicit WorkbenchModel(client::Backend& backend, client::Clock clock) noexcept
	    : model_(backend, clock)
	{}

	WorkbenchModel(const WorkbenchModel&) = delete;
	WorkbenchModel& operator=(const WorkbenchModel&) = delete;
	WorkbenchModel(WorkbenchModel&&) = delete;
	WorkbenchModel& operator=(WorkbenchModel&&) = delete;

	[[nodiscard]] client::Status admit(const runtime::RuntimeSchema& schema, client::Input header,
	                                   std::uint32_t session) noexcept
	{
		const auto status = model_.admit(schema, header, session);
		if (status == client::Status::Ok)
			cancelEdit();
		return status;
	}

	[[nodiscard]] auto catalogs(descriptor::Category category) const noexcept
	{
		return model_.catalogs(category);
	}

	[[nodiscard]] auto endpoints(descriptor::Category category) const noexcept
	{
		return model_.endpoints(category);
	}

	[[nodiscard]] auto fields() const noexcept
	{
		return model_.fields();
	}

	[[nodiscard]] auto commands() const noexcept
	{
		return model_.commands();
	}

	[[nodiscard]] auto services() const noexcept
	{
		return model_.services();
	}

	[[nodiscard]] auto bindField(std::uint32_t id) const noexcept
	{
		return model_.bindField(id);
	}

	[[nodiscard]] client::Status selectField(const dynamic::EndpointBinding& field) noexcept
	{
		const auto status = model_.selectField(field);
		if (status == client::Status::Ok)
			cancelEdit();
		return status;
	}

	void clearSelection() noexcept
	{
		cancelEdit();
		model_.clearSelection();
	}

	template<class F>
	[[nodiscard]] inspector::ReadSubmission readField(const dynamic::EndpointBinding& field,
	                                                  F&& callback,
	                                                  std::uint32_t timeout = 30000) noexcept
	{
		const auto submitted = model_.readField(field, std::forward<F>(callback), timeout);
		if (submitted)
			cancelEdit();
		return submitted;
	}

	template<class F>
	[[nodiscard]] inspector::ReadSubmission refresh(F&& callback,
	                                                std::uint32_t timeout = 30000) noexcept
	{
		const auto submitted = model_.refresh(std::forward<F>(callback), timeout);
		if (submitted)
			cancelEdit();
		return submitted;
	}

	[[nodiscard]] auto snapshotInfo() const noexcept
	{
		return model_.snapshotInfo();
	}

	[[nodiscard]] auto root() const noexcept
	{
		return model_.root();
	}

	[[nodiscard]] auto info(const inspector::NodeHandle& node) const noexcept
	{
		return model_.info(node);
	}

	[[nodiscard]] auto childCount(const inspector::NodeHandle& node) const noexcept
	{
		return model_.childCount(node);
	}

	[[nodiscard]] auto child(const inspector::NodeHandle& node, std::uint32_t index) const noexcept
	{
		return model_.child(node, index);
	}

	[[nodiscard]] auto scalar(const inspector::NodeHandle& node) const noexcept
	{
		return model_.scalar(node);
	}

	[[nodiscard]] auto decimal(const inspector::NodeHandle& node,
	                           std::span<char> output) const noexcept
	{
		return model_.decimal(node, output);
	}

	[[nodiscard]] bool isCurrent() const noexcept
	{
		return model_.isCurrent();
	}

	[[nodiscard]] bool isCurrent(const dynamic::EndpointBinding& field) const noexcept
	{
		return model_.isCurrent(field);
	}

	void invalidate() noexcept
	{
		cancelEdit();
		model_.invalidate();
	}

	[[nodiscard]] bool confirmQuiescence(dynamic::Quiescence proof) noexcept
	{
		return model_.confirmQuiescence(proof);
	}

	void process(std::uint32_t now) noexcept
	{
		model_.process(now);
		if (phase_ == Phase::Editing && !editCurrent())
			cancelEdit();
	}

	[[nodiscard]] auto nextPacket() noexcept
	{
		return model_.nextPacket();
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t session, std::uint32_t request) noexcept
	{
		return model_.prepareHandoff(session, request);
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t session, std::uint32_t request) noexcept
	{
		return model_.onSendAccepted(session, request);
	}

	[[nodiscard]] bool onReply(client::Input input, std::uint32_t session,
	                           std::uint32_t request) noexcept
	{
		return model_.onReply(input, session, request);
	}

	[[nodiscard]] bool cancel(client::RequestHandle request) noexcept
	{
		return model_.cancel(request);
	}

	[[nodiscard]] std::size_t pending() const noexcept
	{
		return model_.pending();
	}

	[[nodiscard]] bool drained() const noexcept
	{
		return !completionActive_ && model_.drained();
	}

	[[nodiscard]] bool recoveryRequired() const noexcept
	{
		return model_.recoveryRequired();
	}

	[[nodiscard]] std::uint32_t session() const noexcept
	{
		return model_.session();
	}

	void close() noexcept
	{
		cancelEdit();
		model_.close();
	}

	[[nodiscard]] Phase phase() const noexcept
	{
		return phase_;
	}

	[[nodiscard]] bool editing() const noexcept
	{
		return phase_ == Phase::Editing && editCurrent();
	}

	[[nodiscard]] bool canEdit() const noexcept
	{
		if (phase_ != Phase::Idle || !model_.isCurrent())
			return false;
		const auto snapshot = model_.snapshotInfo();
		return snapshot.ready && snapshot.bytes <= PayloadBytes &&
		       bool(model_.client_.bind(client::Operation::FieldWrite, snapshot.fieldId));
	}

	// Dirty compares bytes only when requested; scalar updates never copy or
	// recursively validate the whole DTO, and editing back restores dirty=false.
	[[nodiscard]] bool dirty() const noexcept
	{
		if (!editing())
			return false;
		const auto original = model_.snapshot_.bytes();
		return original &&
		       !std::equal(original.value.begin(), original.value.end(), draft_.begin());
	}

	[[nodiscard]] bool canApply() const noexcept
	{
		return editing() && dirty() && !completionActive_;
	}

	[[nodiscard]] runtime::Status beginEdit() noexcept
	{
		if (phase_ != Phase::Idle || !model_.isCurrent())
			return runtime::Status::Stale;
		const auto snapshot = model_.snapshotInfo();
		if (!snapshot.ready)
			return runtime::Status::Stale;
		if (snapshot.bytes > PayloadBytes)
			return runtime::Status::Capacity;
		const auto writable = model_.client_.bind(client::Operation::FieldWrite, snapshot.fieldId);
		if (!writable)
			return runtime::Status::TypeMismatch;
		const auto original = model_.snapshot_.bytes();
		if (!original)
			return runtime::Status::Stale;
		auto writer = runtime::DynamicValueWriter::begin(model_.schema(), snapshot.type,
		                                                 original.value, draft_);
		if (!writer)
			return writer.status();
		writer_ = std::move(writer);
		editRead_ = model_.selection_;
		editWrite_ = writable.endpoint;
		editAdmission_ = snapshot.admission;
		editRevision_ = snapshot.revision;
		editBytes_ = snapshot.bytes;
		phase_ = Phase::Editing;
		return runtime::Status::Ok;
	}

	template<class T>
	[[nodiscard]] runtime::Status setScalar(const inspector::NodeHandle& node,
	                                        const T& value) noexcept
	{
		static_assert(runtime::value_detail::scalar<T>, "Use the exact fixed-width scalar type");
		if (!editing())
			return runtime::Status::Stale;
		const auto metadata = model_.snapshot_.info(node);
		const auto prepared = model_.snapshot_.prepareScalar(node);
		if (!metadata || !prepared)
			return !metadata && metadata.status == inspector::ModelStatus::Stale
			           ? runtime::Status::Stale
			           : runtime::Status::TypeMismatch;
		// Refused UI input never enters the Writer's sticky-error path.
		if (metadata.value.code != runtime::value_detail::scalarCode<T>())
			return runtime::Status::TypeMismatch;
		return metadata.value.kind == TypeKind::Enum
		           ? writer_.setEnumUnderlying(prepared.value, value)
		           : writer_.set(prepared.value, value);
	}

	[[nodiscard]] inspector::Result<inspector::ScalarValue>
	editScalar(const inspector::NodeHandle& node) const noexcept
	{
		if (!editing())
			return {inspector::ModelStatus::Stale, {}};
		return model_.snapshot_.scalar(node, client::Input{draft_}.first(editBytes_));
	}

	void cancelEdit() noexcept
	{
		if (phase_ != Phase::Editing)
			return;
		writer_ = {};
		editBytes_ = 0;
		phase_ = Phase::Idle;
	}

	template<class F>
	[[nodiscard]] inspector::ReadSubmission apply(F&& callback, std::uint32_t writeTimeout = 30000,
	                                              std::uint32_t refreshTimeout = 30000) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<std::decay_t<F>&, const ApplyResult&>,
		              "Workbench completion must be noexcept");
		const auto gate = model_.submissionGate();
		if (gate != client::Status::Ok)
			return {gate, inspector::ModelStatus::Ok, {}};
		if (completionActive_ || phase_ == Phase::Writing || phase_ == Phase::Refreshing)
			return {client::Status::Busy, inspector::ModelStatus::Ok, {}};
		if (!canApply())
			return {client::Status::InvalidArgument, inspector::ModelStatus::NoSnapshot, {}};
		if (!writeTimeout || writeTimeout > client::maxTimeoutMs || !refreshTimeout ||
		    refreshTimeout > client::maxTimeoutMs)
			return {client::Status::InvalidArgument, inspector::ModelStatus::Ok, {}};
		if constexpr (std::is_pointer_v<std::decay_t<F>>)
			if (callback == nullptr)
				return {client::Status::InvalidArgument, inspector::ModelStatus::Ok, {}};
		const auto committed = writer_.commit(std::span{request_}.first(editBytes_));
		if (!committed)
			return {client::Status::CodecError, inspector::ModelStatus::InvalidPayload, {}};
		completion_.bind(std::forward<F>(callback));
		applyResult_ = {};
		const auto submitted = model_.client_.writeField(
		    editWrite_, client::Input{request_}.first(editBytes_),
		    [this, refreshTimeout](const dynamic::DynamicResult& result) noexcept {
			    writeCompleted(result, refreshTimeout);
		    },
		    writeTimeout);
		if (!submitted) {
			completion_.reset();
			// commit consumed its Writer; retain the committed payload and rebuild
			// the lease so a refused submission neither discards nor freezes edits.
			writer_ = runtime::DynamicValueWriter::begin(model_.schema(), editRead_.responseType(),
			                                             client::Input{request_}.first(editBytes_),
			                                             draft_);
			return {submitted.status, inspector::ModelStatus::Ok, {}};
		}
		writer_ = {};
		model_.snapshot_.retire();
		applyAdmission_ = model_.snapshot_.admission();
		applyRevision_ = model_.snapshot_.revision();
		phase_ = Phase::Writing;
		return {client::Status::Ok, inspector::ModelStatus::Ok, submitted.handle};
	}

private:
	inspector::InspectorModel<N, PayloadBytes, SnapshotBytes, CallbackBytes> model_;
	std::array<std::byte, PayloadBytes> draft_{}, request_{};
	runtime::DynamicValueWriter writer_;
	dynamic::EndpointBinding editRead_{}, editWrite_{};
	std::uint64_t editAdmission_ = 0, editRevision_ = 0, applyAdmission_ = 0, applyRevision_ = 0;
	std::size_t editBytes_ = 0;
	Phase phase_ = Phase::Idle;
	bool completionActive_ = false;
	DelegateSlot<void(const ApplyResult&) noexcept, CallbackBytes> completion_;
	ApplyResult applyResult_{};

	[[nodiscard]] bool editCurrent() const noexcept
	{
		if (!model_.isCurrent(editRead_) || !model_.isCurrent(editWrite_))
			return false;
		const auto snapshot = model_.snapshotInfo();
		return snapshot.ready && snapshot.admission == editAdmission_ &&
		       snapshot.revision == editRevision_ && snapshot.bytes == editBytes_;
	}

	void finishApply() noexcept
	{
		phase_ = Phase::Idle;
		const auto result = applyResult_;
		completionActive_ = true;
		completion_.invoke(result);
		completion_.reset();
		completionActive_ = false;
	}

	void writeCompleted(const dynamic::DynamicResult& result, std::uint32_t refreshTimeout) noexcept
	{
		const auto& operation = result.operation;
		applyResult_.write = {operation.status, operation.operation, operation.dispatch,
		                      operation.endpointStatus, operation.codec};
		applyResult_.writeCompleted = true;
		if (!operation.succeeded()) {
			finishApply();
			return;
		}
		applyResult_.refreshAttempted = true;
		if (!model_.isCurrent(editRead_)) {
			applyResult_.refreshSubmission = model_.submissionGate();
			if (applyResult_.refreshSubmission == client::Status::Ok)
				applyResult_.refreshSubmission = client::Status::NotAdmitted;
			finishApply();
			return;
		}
		if (model_.snapshot_.admission() != applyAdmission_ ||
		    model_.snapshot_.revision() != applyRevision_) {
			applyResult_.refreshSubmission = client::Status::Cancelled;
			finishApply();
			return;
		}
		phase_ = Phase::Refreshing;
		const auto submitted = model_.readField(
		    editRead_,
		    [this](const inspector::ReadResult& read) noexcept {
			    applyResult_.refresh = read;
			    applyResult_.refreshCompleted = true;
			    finishApply();
		    },
		    refreshTimeout);
		applyResult_.refreshSubmission = submitted.status;
		applyResult_.refreshSubmitted = bool(submitted);
		if (!submitted)
			finishApply();
	}
};

} // namespace telemetry::integration::workbench
#endif // TELEMETRY_INTEGRATION_WORKBENCH_WORKBENCH_MODEL_HPP
