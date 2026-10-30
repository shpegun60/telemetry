/*
 * @file WorkbenchModel.hpp
 * @brief Shared FieldWrite and Command editing over the Inspector's single runtime client.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_INTEGRATION_WORKBENCH_WORKBENCH_MODEL_HPP
#define TELEMETRY_INTEGRATION_WORKBENCH_WORKBENCH_MODEL_HPP
#pragma once

#include <integration/inspector/InspectorModel.hpp>
#include "RequestEditor.hpp"
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
	Refreshing,
	Executing
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
	    : model_(backend, clock),
	      commandStore_(request_,
	                    {this,
	                     +[](const void* context) noexcept {
		                     return static_cast<const WorkbenchModel*>(context)->model_.isCurrent();
	                     }}),
	      editor_(draft_, request_)
	{}

	WorkbenchModel(const WorkbenchModel&) = delete;
	WorkbenchModel& operator=(const WorkbenchModel&) = delete;
	WorkbenchModel(WorkbenchModel&&) = delete;
	WorkbenchModel& operator=(WorkbenchModel&&) = delete;

	[[nodiscard]] client::Status admit(const runtime::RuntimeSchema& schema, client::Input header,
	                                   std::uint32_t session) noexcept
	{
		if (publishing_)
			return publicationStatus();
		if (!commandStore_.canAdmit()) {
			const auto gate = model_.submissionGate();
			return gate == client::Status::Closed || gate == client::Status::RecoveryRequired
			           ? gate
			           : client::Status::IdsExhausted;
		}
		const auto status = model_.admit(schema, header, session);
		if (status == client::Status::Ok) {
			cancelEdit();
			if (commandStore_.admit(schema, session) != inspector::ModelStatus::Ok) {
				model_.invalidate();
				return client::Status::InvalidArgument;
			}
		}
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
		if (publishing_)
			return publicationStatus();
		if (phase_ == Phase::Executing)
			return client::Status::Busy;
		const auto status = model_.selectField(field);
		if (status == client::Status::Ok)
			cancelEdit();
		return status;
	}

	void clearSelection() noexcept
	{
		if (publishing_ || phase_ == Phase::Executing)
			return;
		cancelEdit();
		model_.clearSelection();
	}

	template<class F>
	[[nodiscard]] inspector::ReadSubmission readField(const dynamic::EndpointBinding& field,
	                                                  F&& callback,
	                                                  std::uint32_t timeout = 30000) noexcept
	{
		if (publishing_)
			return {publicationStatus(), inspector::ModelStatus::Ok, {}};
		if (phase_ == Phase::Executing)
			return {client::Status::Busy, inspector::ModelStatus::Ok, {}};
		const auto submitted = model_.readField(field, std::forward<F>(callback), timeout);
		if (submitted)
			cancelEdit();
		return submitted;
	}

	template<class F>
	[[nodiscard]] inspector::ReadSubmission refresh(F&& callback,
	                                                std::uint32_t timeout = 30000) noexcept
	{
		if (publishing_)
			return {publicationStatus(), inspector::ModelStatus::Ok, {}};
		if (commandSelected_ || phase_ == Phase::Executing)
			return {client::Status::Busy, inspector::ModelStatus::Ok, {}};
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
		retireEditor();
		model_.invalidate();
	}

	[[nodiscard]] bool confirmQuiescence(dynamic::Quiescence proof) noexcept
	{
		return !publishing_ && model_.confirmQuiescence(proof);
	}

	void process(std::uint32_t now) noexcept
	{
		if (publishing_)
			return;
		model_.process(now);
		if (phase_ == Phase::Editing && !editCurrent())
			cancelEdit();
	}

	[[nodiscard]] auto nextPacket() noexcept
	{
		return publishing_ ? client::Packet{} : model_.nextPacket();
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t session, std::uint32_t request) noexcept
	{
		return !publishing_ && model_.prepareHandoff(session, request);
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t session, std::uint32_t request) noexcept
	{
		return !publishing_ && model_.onSendAccepted(session, request);
	}

	[[nodiscard]] bool onReply(client::Input input, std::uint32_t session,
	                           std::uint32_t request) noexcept
	{
		return !publishing_ && model_.onReply(input, session, request);
	}

	[[nodiscard]] bool cancel(client::RequestHandle request) noexcept
	{
		return !publishing_ && model_.cancel(request);
	}

	[[nodiscard]] std::size_t pending() const noexcept
	{
		return model_.pending();
	}

	[[nodiscard]] bool drained() const noexcept
	{
		return !publishing_ && !completionActive_ && model_.drained();
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
		retireEditor();
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
		if (publishing_ || commandSelected_ || phase_ != Phase::Idle || !model_.isCurrent())
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
		return editor_.dirty();
	}

	[[nodiscard]] bool canApply() const noexcept
	{
		return !publishing_ && !commandSelected_ && editing() && dirty() && !completionActive_;
	}

	[[nodiscard]] runtime::Status beginEdit() noexcept
	{
		if (publishing_ || commandSelected_ || phase_ != Phase::Idle || !model_.isCurrent())
			return runtime::Status::Stale;
		const auto snapshot = model_.snapshotInfo();
		if (!snapshot.ready)
			return runtime::Status::Stale;
		if (snapshot.bytes > PayloadBytes)
			return runtime::Status::Capacity;
		const auto writable = model_.client_.bind(client::Operation::FieldWrite, snapshot.fieldId);
		if (!writable)
			return runtime::Status::TypeMismatch;
		const auto status = editor_.begin(model_.snapshot_);
		if (status != runtime::Status::Ok)
			return status;
		editRead_ = model_.selection_;
		editWrite_ = writable.endpoint;
		phase_ = Phase::Editing;
		return runtime::Status::Ok;
	}

	template<class T>
	[[nodiscard]] runtime::Status setScalar(const inspector::NodeHandle& node,
	                                        const T& value) noexcept
	{
		if (publishing_ || !editing())
			return runtime::Status::Stale;
		return editor_.setScalar(node, value);
	}

	[[nodiscard]] inspector::Result<inspector::ScalarValue>
	editScalar(const inspector::NodeHandle& node) const noexcept
	{
		if (!editing())
			return {inspector::ModelStatus::Stale, {}};
		return editor_.scalar(node);
	}

	void cancelEdit() noexcept
	{
		if (publishing_ || phase_ != Phase::Editing)
			return;
		retireEditor();
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
		if (publishing_ || completionActive_ || phase_ == Phase::Writing ||
		    phase_ == Phase::Refreshing || phase_ == Phase::Executing)
			return {client::Status::Busy, inspector::ModelStatus::Ok, {}};
		if (!canApply())
			return {client::Status::InvalidArgument, inspector::ModelStatus::NoSnapshot, {}};
		if (!writeTimeout || writeTimeout > client::maxTimeoutMs || !refreshTimeout ||
		    refreshTimeout > client::maxTimeoutMs)
			return {client::Status::InvalidArgument, inspector::ModelStatus::Ok, {}};
		if constexpr (std::is_pointer_v<std::decay_t<F>>)
			if (callback == nullptr)
				return {client::Status::InvalidArgument, inspector::ModelStatus::Ok, {}};
		const Publication publication{publishing_};
		const auto committed = editor_.commit();
		if (!committed)
			return {client::Status::CodecError, inspector::ModelStatus::InvalidPayload, {}};
		completion_.bind(
		    [fn = std::forward<F>(callback)](const Completion& result) mutable noexcept {
			    fn(result.field);
		    });
		applyResult_ = {};
		const auto submitted = model_.client_.writeField(
		    editWrite_, committed.value,
		    [this, refreshTimeout](const dynamic::DynamicResult& result) noexcept {
			    writeCompleted(result, refreshTimeout);
		    },
		    writeTimeout);
		if (!submitted) {
			completion_.reset();
			// commit consumed its Writer; retain the committed payload and rebuild
			// the lease so a refused submission neither discards nor freezes edits.
			(void)editor_.rearm();
			return {submitted.status, inspector::ModelStatus::Ok, {}};
		}
		editor_.clear();
		model_.snapshot_.retire();
		applyAdmission_ = model_.snapshot_.admission();
		applyRevision_ = model_.snapshot_.revision();
		phase_ = Phase::Writing;
		return {client::Status::Ok, inspector::ModelStatus::Ok, submitted.handle};
	}

	[[nodiscard]] dynamic::EndpointResult bindCommand(std::uint32_t id) const noexcept
	{
		const auto gate = model_.submissionGate();
		return gate == client::Status::Ok ? model_.client_.bind(client::Operation::Command, id)
		                                  : dynamic::EndpointResult{gate, {}};
	}

	[[nodiscard]] client::Status selectCommand(const dynamic::EndpointBinding& command) noexcept
	{
		const auto gate = model_.submissionGate();
		if (gate != client::Status::Ok)
			return gate;
		if (publishing_ || completionActive_ || phase_ == Phase::Writing ||
		    phase_ == Phase::Refreshing || phase_ == Phase::Executing ||
		    (phase_ == Phase::Editing && !commandSelected_))
			return client::Status::Busy;
		if (!model_.client_.isCurrent(command))
			return client::Status::NotAdmitted;
		if (command.operation() != client::Operation::Command)
			return client::Status::InvalidArgument;
		const auto rebound = bindCommand(command.id());
		if (!rebound)
			return rebound.status;
		if (command.requestBytes() > request_.size())
			return client::Status::BufferTooSmall;
		if (!commandStore_.canAdvance())
			return client::Status::IdsExhausted;
		const auto initialized =
		    RequestEditor::initialize(model_.schema(), command.requestType(),
		                              std::span{request_}.first(command.requestBytes()));
		if (initialized != runtime::Status::Ok)
			return client::Status::CodecError;
		editor_.clear();
		const auto selected = commandStore_.selectEndpoint(command.id(), command.requestType(),
		                                                   descriptor::Category::Command);
		if (selected != inspector::ModelStatus::Ok)
			return client::Status::NotAdmitted;
		if (command.requestType() != 0 &&
		    commandStore_.publish(client::Input{request_}.first(command.requestBytes()),
		                          commandStore_.admission(),
		                          commandStore_.revision()) != inspector::ModelStatus::Ok)
			return client::Status::CodecError;
		const auto started = editor_.begin(commandStore_);
		if (started != runtime::Status::Ok)
			return client::Status::CodecError;
		command_ = command;
		commandSelected_ = true;
		phase_ = Phase::Editing;
		return client::Status::Ok;
	}

	[[nodiscard]] bool commandSelected() const noexcept
	{
		return commandSelected_ && model_.isCurrent(command_);
	}

	[[nodiscard]] bool canExecute() const noexcept
	{
		return !publishing_ && commandSelected() && phase_ == Phase::Editing &&
		       !completionActive_ && model_.submissionGate() == client::Status::Ok &&
		       editor_.current();
	}

	[[nodiscard]] auto editorSnapshotInfo() const noexcept
	{
		return commandSelected() ? editor_.snapshotInfo() : inspector::SnapshotInfo{};
	}

	[[nodiscard]] auto editorRoot() const noexcept
	{
		return commandSelected() ? editor_.root()
		                         : inspector::NodeResult{inspector::ModelStatus::Stale, {}};
	}

	[[nodiscard]] auto editorInfo(const inspector::NodeHandle& node) const noexcept
	{
		return commandSelected()
		           ? editor_.info(node)
		           : inspector::Result<inspector::NodeInfo>{inspector::ModelStatus::Stale, {}};
	}

	[[nodiscard]] auto editorChild(const inspector::NodeHandle& node,
	                               std::uint32_t index) const noexcept
	{
		return commandSelected() ? editor_.child(node, index)
		                         : inspector::NodeResult{inspector::ModelStatus::Stale, {}};
	}

	[[nodiscard]] auto editorChildCount(const inspector::NodeHandle& node) const noexcept
	{
		return commandSelected()
		           ? editor_.childCount(node)
		           : inspector::Result<std::uint32_t>{inspector::ModelStatus::Stale, 0};
	}

	template<class F>
	[[nodiscard]] inspector::ReadSubmission execute(F&& callback,
	                                                std::uint32_t timeout = 30000) noexcept
	{
		static_assert(
		    std::is_nothrow_invocable_v<std::decay_t<F>&, const inspector::OperationResult&>,
		    "Command completion must be noexcept");
		const auto gate = model_.submissionGate();
		if (gate != client::Status::Ok)
			return {gate, inspector::ModelStatus::Ok, {}};
		if (publishing_ || completionActive_ || phase_ == Phase::Writing ||
		    phase_ == Phase::Refreshing || phase_ == Phase::Executing)
			return {client::Status::Busy, inspector::ModelStatus::Ok, {}};
		if (!canExecute())
			return {client::Status::InvalidArgument, inspector::ModelStatus::NoSelection, {}};
		if (!timeout || timeout > client::maxTimeoutMs)
			return {client::Status::InvalidArgument, inspector::ModelStatus::Ok, {}};
		if constexpr (std::is_pointer_v<std::decay_t<F>>)
			if (callback == nullptr)
				return {client::Status::InvalidArgument, inspector::ModelStatus::Ok, {}};
		const Publication publication{publishing_};
		const auto committed = editor_.commit();
		if (!committed)
			return {client::Status::CodecError, inspector::ModelStatus::InvalidPayload, {}};
		completion_.bind(
		    [fn = std::forward<F>(callback)](const Completion& result) mutable noexcept {
			    fn(result.command);
		    });
		const auto submitted = model_.client_.command(
		    command_, committed.value,
		    [this](const dynamic::DynamicResult& result) noexcept {
			    commandCompleted(result.operation);
		    },
		    timeout);
		(void)editor_.rearm();
		if (!submitted) {
			completion_.reset();
			return {submitted.status, inspector::ModelStatus::Ok, {}};
		}
		model_.snapshot_.retire();
		phase_ = Phase::Executing;
		return {client::Status::Ok, inspector::ModelStatus::Ok, submitted.handle};
	}

private:
	inspector::InspectorModel<N, PayloadBytes, SnapshotBytes, CallbackBytes> model_;
	std::array<std::byte, PayloadBytes> draft_{}, request_{};
	inspector::SnapshotStore commandStore_;
	RequestEditor editor_;
	dynamic::EndpointBinding editRead_{}, editWrite_{};
	dynamic::EndpointBinding command_{};
	std::uint64_t applyAdmission_ = 0, applyRevision_ = 0;
	Phase phase_ = Phase::Idle;
	bool completionActive_ = false, commandSelected_ = false, publishing_ = false;

	// Owning a callable may run its user-defined move/destructor. Keep request,
	// binding and completion publication indivisible to those owner reentries.
	// Revocation remains allowed; no callbacks or transport progress occur here.
	struct Publication {
		bool& active;

		explicit Publication(bool& flag) noexcept : active(flag)
		{
			active = true;
		}

		~Publication()
		{
			active = false;
		}
	};

	struct Completion {
		ApplyResult field{};
		inspector::OperationResult command{};
	};

	DelegateSlot<void(const Completion&) noexcept, CallbackBytes> completion_;
	ApplyResult applyResult_{};

	[[nodiscard]] bool editCurrent() const noexcept
	{
		return editor_.current() &&
		       (commandSelected_ ? model_.isCurrent(command_)
		                         : (model_.isCurrent(editRead_) && model_.isCurrent(editWrite_)));
	}

	[[nodiscard]] client::Status publicationStatus() const noexcept
	{
		const auto gate = model_.submissionGate();
		return gate == client::Status::Ok ? client::Status::Busy : gate;
	}

	void retireEditor() noexcept
	{
		editor_.clear();
		clearCommand();
		if (phase_ == Phase::Editing)
			phase_ = Phase::Idle;
	}

	void clearCommand() noexcept
	{
		if (commandSelected_) {
			editor_.clear();
			commandSelected_ = false;
			commandStore_.clearSelection();
		}
	}

	void commandCompleted(const client::ResultView& operation) noexcept
	{
		const inspector::OperationResult result{operation.status, operation.operation,
		                                        operation.dispatch, operation.endpointStatus,
		                                        operation.codec};
		if (commandSelected() && editor_.current() && model_.submissionGate() == client::Status::Ok)
			phase_ = Phase::Editing;
		else {
			clearCommand();
			phase_ = Phase::Idle;
		}
		completionActive_ = true;
		completion_.invoke(Completion{{}, result});
		{
			const Publication publication{publishing_};
			completion_.reset();
		}
		completionActive_ = false;
	}

	void finishApply() noexcept
	{
		phase_ = Phase::Idle;
		const auto result = applyResult_;
		completionActive_ = true;
		completion_.invoke(Completion{result, {}});
		{
			const Publication publication{publishing_};
			completion_.reset();
		}
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
