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

namespace telemetry::integration::inspector {

enum class ModelStatus : std::uint8_t {
	Ok,
	NoSelection,
	NoSnapshot,
	Stale,
	TypeMismatch,
	IndexOutOfRange,
	SnapshotTooLarge,
	RevisionExhausted,
	InvalidPayload,
	BufferTooSmall
};

template<class T>
struct Result {
	ModelStatus status = ModelStatus::Stale;
	T value{};

	[[nodiscard]] explicit operator bool() const noexcept
	{
		return status == ModelStatus::Ok;
	}
};

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

// Handles borrow exactly one model lifetime and must not survive its destruction.
// Constructing another model at the same address does not extend that lifetime;
// these checks do not distinguish handles retained from a destroyed instance.
// Handles never contain pointers to rows or payload. A new accepted read,
// selection, admission or invalidation retires them within the live instance.
class NodeHandle {
public:
	NodeHandle() noexcept = default;

private:
	const void* owner_ = nullptr;
	std::uint64_t admission_ = 0, revision_ = 0;
	TypeId type_ = 0, parentType_ = 0;
	std::uint32_t offset_ = 0, index_ = 0;
	bool member_ = false, arrayElement_ = false;
	template<std::size_t, std::size_t, std::size_t, std::size_t>
	friend class InspectorModel;
};

using NodeResult = Result<NodeHandle>;

struct NodeInfo {
	TypeKind kind = TypeKind::Void;
	TypeId type = 0;
	// code applies only to scalar/enum nodes; enums expose their underlying code.
	ScalarCode code = ScalarCode::Bool;
	std::uint32_t children = 0, index = 0;
	bool arrayElement = false;
	// Names, like the metadata enumeration spans, borrow the live schema generation.
	std::string_view name{};
};

using ScalarData =
    std::variant<bool, std::uint8_t, std::int8_t, std::uint16_t, std::int16_t, std::uint32_t,
                 std::int32_t, std::uint64_t, std::int64_t, float, double>;

struct ScalarValue {
	ScalarCode code = ScalarCode::Bool;
	ScalarData value{};
	bool enumValue = false, enumKnown = false;
	// Numeric value is owned. Only this optional name borrows schema storage.
	std::string_view enumName{};
};

struct SnapshotInfo {
	std::uint32_t fieldId = 0, session = 0;
	TypeId type = 0;
	std::uint64_t admission = 0, revision = 0;
	std::size_t bytes = 0;
	bool selected = false, ready = false;
};

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
	    : client_(backend, clock)
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
		if (admission_ == std::numeric_limits<std::uint64_t>::max())
			return client::Status::IdsExhausted;
		const auto status = client_.admit(schema, observedHeader, session);
		if (status != client::Status::Ok)
			return status;
		schema_ = schema;
		admitted_ = true;
		++admission_;
		revision_ = 0;
		selected_ = false;
		ready_ = false;
		return client::Status::Ok;
	}

	[[nodiscard]] std::span<const runtime::RuntimeCatalog>
	catalogs(descriptor::Category category) const noexcept
	{
		return current() ? schema_.catalogs(category) : std::span<const runtime::RuntimeCatalog>{};
	}

	[[nodiscard]] std::span<const runtime::RuntimeEndpoint>
	endpoints(descriptor::Category category) const noexcept
	{
		return current() ? schema_.endpoints(category)
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
		if (!advanceRevision())
			return client::Status::IdsExhausted;
		selection_ = field;
		selected_ = true;
		return client::Status::Ok;
	}

	void clearSelection() noexcept
	{
		retireSnapshot();
		selected_ = false;
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
		if (field.responseBytes() > SnapshotBytes)
			return {client::Status::BufferTooSmall, ModelStatus::SnapshotTooLarge, {}};
		if (revision_ == std::numeric_limits<std::uint64_t>::max())
			return {client::Status::IdsExhausted, ModelStatus::RevisionExhausted, {}};
		const auto revision = revision_ + 1;
		const auto admission = admission_;
		const auto id = field.id();
		const auto submitted = client_.readField(
		    field,
		    [this, admission, revision, id, fn = std::forward<F>(callback)](
		        const dynamic::DynamicResult& result) mutable noexcept {
			    const auto completed = publish(result, admission, revision, id);
			    fn(completed);
		    },
		    timeoutMs);
		if (!submitted)
			return {submitted.status, ModelStatus::Ok, {}};
		revision_ = revision;
		ready_ = false;
		selection_ = field;
		selected_ = true;
		return {client::Status::Ok, ModelStatus::Ok, submitted.handle};
	}

	template<class F>
	[[nodiscard]] ReadSubmission refresh(F&& callback, std::uint32_t timeoutMs = 30000) noexcept
	{
		const auto gate = submissionGate();
		if (gate != client::Status::Ok)
			return {gate, ModelStatus::Ok, {}};
		if (!selected_)
			return {client::Status::InvalidArgument, ModelStatus::NoSelection, {}};
		return readField(selection_, std::forward<F>(callback), timeoutMs);
	}

	[[nodiscard]] SnapshotInfo snapshotInfo() const noexcept
	{
		if (!current())
			return {};
		return {selected_ ? selection_.id() : 0,
		        client_.session(),
		        selected_ ? selection_.responseType() : 0,
		        admission_,
		        revision_,
		        ready_ ? snapshotSize_ : 0,
		        selected_,
		        ready_};
	}

	[[nodiscard]] NodeResult root() const noexcept
	{
		if (!current())
			return {ModelStatus::Stale, {}};
		if (!ready_)
			return {ModelStatus::NoSnapshot, {}};
		return {ModelStatus::Ok, makeNode(selection_.responseType(), 0)};
	}

	[[nodiscard]] Result<NodeInfo> info(const NodeHandle& node) const noexcept
	{
		const auto checked = checkNode(node);
		if (!checked)
			return {checked.status, {}};
		const auto& type = *checked.value;
		NodeInfo result{type.kind,          type.id, type.scalarCode, children(type), node.index_,
		                node.arrayElement_, {}};
		if (type.kind == TypeKind::Enum) {
			const auto* underlying = schema_.type(type.relatedTypeId);
			if (!underlying)
				return {ModelStatus::InvalidPayload, {}};
			result.code = underlying->scalarCode;
		}
		if (node.member_) {
			const auto members = schema_.members(node.parentType_);
			if (node.index_ >= members.size())
				return {ModelStatus::InvalidPayload, {}};
			result.name = members[node.index_].name;
		} else if (!node.arrayElement_) {
			for (const auto& endpoint : schema_.endpoints(descriptor::Category::Field))
				if (endpoint.id == selection_.id()) {
					result.name = endpoint.name;
					break;
				}
		}
		return {ModelStatus::Ok, result};
	}

	[[nodiscard]] Result<std::uint32_t> childCount(const NodeHandle& node) const noexcept
	{
		const auto checked = checkNode(node);
		return checked ? Result<std::uint32_t>{ModelStatus::Ok, children(*checked.value)}
		               : Result<std::uint32_t>{checked.status, 0};
	}

	[[nodiscard]] NodeResult child(const NodeHandle& parent, std::uint32_t index) const noexcept
	{
		const auto checked = checkNode(parent);
		if (!checked)
			return {checked.status, {}};
		const auto& type = *checked.value;
		if (type.kind != TypeKind::Struct && type.kind != TypeKind::Array)
			return {ModelStatus::TypeMismatch, {}};
		if (index >= children(type))
			return {ModelStatus::IndexOutOfRange, {}};
		TypeId childType = 0;
		std::uint64_t relative = 0;
		if (type.kind == TypeKind::Struct) {
			const auto& member = schema_.members(type.id)[index];
			childType = member.typeId;
			relative = member.wireOffset;
		} else {
			const auto* element = schema_.type(type.relatedTypeId);
			if (!element)
				return {ModelStatus::InvalidPayload, {}};
			childType = element->id;
			relative = std::uint64_t{index} * element->wireBytes;
		}
		const auto offset = std::uint64_t{parent.offset_} + relative;
		if (offset > snapshotSize_ || offset > std::numeric_limits<std::uint32_t>::max())
			return {ModelStatus::InvalidPayload, {}};
		auto result = makeNode(childType, static_cast<std::uint32_t>(offset));
		result.parentType_ = type.id;
		result.index_ = index;
		result.member_ = type.kind == TypeKind::Struct;
		result.arrayElement_ = type.kind == TypeKind::Array;
		return {ModelStatus::Ok, result};
	}

	[[nodiscard]] Result<ScalarValue> scalar(const NodeHandle& node) const noexcept
	{
		const auto checked = checkNode(node);
		if (!checked)
			return {checked.status, {}};
		const auto& type = *checked.value;
		if (type.kind != TypeKind::Scalar && type.kind != TypeKind::Enum)
			return {ModelStatus::TypeMismatch, {}};
		const auto view =
		    schema_.view(type.id, client::Input{snapshot_}.subspan(node.offset_, type.wireBytes));
		if (!view)
			return {ModelStatus::InvalidPayload, {}};
		ScalarValue result;
		result.enumValue = type.kind == TypeKind::Enum;
		result.code = type.scalarCode;
		if (result.enumValue) {
			const auto* underlying = schema_.type(type.relatedTypeId);
			const auto name = view.value.enumName();
			if (!underlying || !name)
				return {ModelStatus::InvalidPayload, {}};
			result.code = underlying->scalarCode;
			result.enumKnown = name.value.known;
			result.enumName = name.value.name;
		}
		switch (result.code) {
			case ScalarCode::Bool:
				return readScalar<bool>(view.value, result);
			case ScalarCode::U8:
				return readScalar<std::uint8_t>(view.value, result);
			case ScalarCode::S8:
				return readScalar<std::int8_t>(view.value, result);
			case ScalarCode::U16:
				return readScalar<std::uint16_t>(view.value, result);
			case ScalarCode::S16:
				return readScalar<std::int16_t>(view.value, result);
			case ScalarCode::U32:
				return readScalar<std::uint32_t>(view.value, result);
			case ScalarCode::S32:
				return readScalar<std::int32_t>(view.value, result);
			case ScalarCode::U64:
				return readScalar<std::uint64_t>(view.value, result);
			case ScalarCode::S64:
				return readScalar<std::int64_t>(view.value, result);
			case ScalarCode::F32:
				return readScalar<float>(view.value, result);
			case ScalarCode::F64:
				return readScalar<double>(view.value, result);
		}
		return {ModelStatus::TypeMismatch, {}};
	}

	// Integer/enum decimal conversion never passes through floating point. Failure
	// leaves output unchanged; output must not alias this model or borrowed schema.
	[[nodiscard]] Result<std::size_t> decimal(const NodeHandle& node,
	                                          std::span<char> output) const noexcept
	{
		const auto value = scalar(node);
		if (!value)
			return {value.status, 0};
		std::array<char, 21> text{};
		std::size_t length = 0;
		const bool integral = std::visit(
		    [&](const auto& number) noexcept {
			    using T = std::remove_cvref_t<decltype(number)>;
			    if constexpr (std::is_integral_v<T>) {
				    const auto converted = [&]() noexcept {
					    if constexpr (std::is_same_v<T, bool>)
						    return std::to_chars(text.data(), text.data() + text.size(),
						                         number ? 1 : 0);
					    else
						    return std::to_chars(text.data(), text.data() + text.size(), number);
				    }();
				    length = static_cast<std::size_t>(converted.ptr - text.data());
				    return converted.ec == std::errc{};
			    } else
				    return false;
		    },
		    value.value.value);
		if (!integral)
			return {ModelStatus::TypeMismatch, 0};
		if (output.size() < length)
			return {ModelStatus::BufferTooSmall, 0};
		std::copy_n(text.data(), length, output.data());
		return {ModelStatus::Ok, length};
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
		admitted_ = false;
		selected_ = false;
		retireSnapshot();
		client_.invalidate();
	}

	[[nodiscard]] bool confirmQuiescence(dynamic::Quiescence proof) noexcept
	{
		const bool confirmed = client_.confirmQuiescence(proof);
		if (confirmed) {
			admitted_ = false;
			selected_ = false;
			retireSnapshot();
		}
		return confirmed;
	}

	void process(std::uint32_t now) noexcept
	{
		(void)current();
		client_.process(now);
		(void)current();
	}

	[[nodiscard]] client::Packet nextPacket() noexcept
	{
		(void)current();
		return client_.nextPacket();
	}

	[[nodiscard]] bool prepareHandoff(std::uint32_t session, std::uint32_t requestId) noexcept
	{
		(void)current();
		return client_.prepareHandoff(session, requestId);
	}

	[[nodiscard]] bool onSendAccepted(std::uint32_t session, std::uint32_t requestId) noexcept
	{
		return client_.onSendAccepted(session, requestId);
	}

	[[nodiscard]] bool onReply(client::Input bytes, std::uint32_t session,
	                           std::uint32_t requestId) noexcept
	{
		return client_.onReply(bytes, session, requestId);
	}

	[[nodiscard]] bool cancel(client::RequestHandle handle) noexcept
	{
		return client_.cancel(handle);
	}

	[[nodiscard]] std::size_t pending() const noexcept
	{
		return client_.pending();
	}

	[[nodiscard]] bool drained() const noexcept
	{
		return client_.drained();
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
		admitted_ = false;
		selected_ = false;
		retireSnapshot();
		client_.close();
	}

private:
	dynamic::DynamicClient<N, PayloadBytes, CallbackBytes> client_;
	runtime::RuntimeSchema schema_{};
	std::array<std::byte, SnapshotBytes> snapshot_{};
	dynamic::EndpointBinding selection_{};
	std::uint64_t admission_ = 0;
	mutable std::uint64_t revision_ = 0;
	std::size_t snapshotSize_ = 0;
	mutable bool admitted_ = false, selected_ = false, ready_ = false;
	bool closed_ = false;

	[[nodiscard]] bool current() const noexcept
	{
		if (!admitted_)
			return false;
		if (!client_.isCurrent()) {
			admitted_ = false;
			selected_ = false;
			retireSnapshot();
			return false;
		}
		return true;
	}

	[[nodiscard]] client::Status submissionGate() const noexcept
	{
		if (closed_)
			return client::Status::Closed;
		if (client_.recoveryRequired())
			return client::Status::RecoveryRequired;
		return current() ? client::Status::Ok : client::Status::NotAdmitted;
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

	void retireSnapshot() const noexcept
	{
		ready_ = false;
		if (revision_ != std::numeric_limits<std::uint64_t>::max())
			++revision_;
	}

	[[nodiscard]] bool advanceRevision() noexcept
	{
		if (revision_ == std::numeric_limits<std::uint64_t>::max())
			return false;
		retireSnapshot();
		return true;
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
		if (admission != admission_ || revision != revision_ || !current() || !selected_ ||
		    !client_.isCurrent(selection_))
			return completed;
		if (!result.hasValue()) {
			completed.modelStatus = result.valueStatus == dynamic::ValueStatus::InvalidPayload
			                            ? ModelStatus::InvalidPayload
			                        : result.valueStatus == dynamic::ValueStatus::TypeMismatch
			                            ? ModelStatus::TypeMismatch
			                            : ModelStatus::NoSnapshot;
			return completed;
		}
		if (operation.payload.size() != selection_.responseBytes() ||
		    operation.payload.size() > snapshot_.size()) {
			completed.modelStatus = ModelStatus::InvalidPayload;
			return completed;
		}
		std::copy(operation.payload.begin(), operation.payload.end(), snapshot_.begin());
		snapshotSize_ = operation.payload.size();
		ready_ = true;
		completed.modelStatus = ModelStatus::Ok;
		completed.published = true;
		return completed;
	}

	[[nodiscard]] NodeHandle makeNode(TypeId type, std::uint32_t offset) const noexcept
	{
		NodeHandle node;
		node.owner_ = this;
		node.admission_ = admission_;
		node.revision_ = revision_;
		node.type_ = type;
		node.offset_ = offset;
		return node;
	}

	[[nodiscard]] Result<const runtime::RuntimeType*>
	checkNode(const NodeHandle& node) const noexcept
	{
		// Identity/revision checks precede every schema row and payload access.
		if (node.owner_ != this || node.admission_ != admission_ || node.revision_ != revision_ ||
		    !current() || !ready_)
			return {ModelStatus::Stale, nullptr};
		const auto* type = schema_.type(node.type_);
		if (!type || node.offset_ > snapshotSize_ || type->wireBytes > snapshotSize_ - node.offset_)
			return {ModelStatus::InvalidPayload, nullptr};
		return {ModelStatus::Ok, type};
	}

	[[nodiscard]] static std::uint32_t children(const runtime::RuntimeType& type) noexcept
	{
		return type.kind == TypeKind::Struct  ? type.memberCount
		       : type.kind == TypeKind::Array ? type.elementCount
		                                      : 0;
	}

	template<class T>
	[[nodiscard]] static Result<ScalarValue> readScalar(const runtime::DynamicValueView& view,
	                                                    ScalarValue value) noexcept
	{
		const auto number = value.enumValue ? view.enumUnderlying<T>() : view.get<T>();
		if (!number)
			return {ModelStatus::InvalidPayload, {}};
		value.value = number.value;
		return {ModelStatus::Ok, value};
	}
};

} // namespace telemetry::integration::inspector
#endif // TELEMETRY_INTEGRATION_INSPECTOR_INSPECTOR_MODEL_HPP
