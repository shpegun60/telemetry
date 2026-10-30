/*
 * @file InspectorController.cpp
 * @brief Own descriptor generations and drive the real Encoded peer from the Qt event loop.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "DemoPeer.hpp"
#include "TcpTransport.hpp"
#ifdef INSPECTOR_ENABLE_SERIAL
#include "SerialTransport.hpp"
#include "../inspector_serial/SerialProfile.hpp"
#include <QtSerialPort/QSerialPortInfo>
#endif
#include <integration/encoded/Backend.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtNetwork/QHostAddress>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <charconv>
#include <limits>
#include <optional>
#include <vector>

namespace inspector_qml {
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace im = telemetry::integration::inspector;
namespace wb = telemetry::integration::workbench;

namespace {
bool mutationPacket(const cl::Packet& packet) noexcept
{
	const auto parsed = telemetry::protocol::encoded::parse(packet.bytes);
	return packet.mayExecute && parsed &&
	       parsed.header.operation == telemetry::protocol::encoded::Operation::FieldWrite;
}

QString statusName(cl::Status status)
{
	switch (status) {
		case cl::Status::Ok:
			return QStringLiteral("Ok");
		case cl::Status::QueueFull:
			return QStringLiteral("QueueFull");
		case cl::Status::PayloadFull:
			return QStringLiteral("PayloadFull");
		case cl::Status::NotAdmitted:
			return QStringLiteral("NotAdmitted");
		case cl::Status::ReadOnly:
			return QStringLiteral("ReadOnly");
		case cl::Status::Closed:
			return QStringLiteral("Closed");
		case cl::Status::Cancelled:
			return QStringLiteral("Cancelled");
		case cl::Status::Timeout:
			return QStringLiteral("Timeout");
		case cl::Status::SessionLost:
			return QStringLiteral("SessionLost");
		case cl::Status::OutcomeUnknown:
			return QStringLiteral("OutcomeUnknown");
		case cl::Status::MalformedReply:
			return QStringLiteral("MalformedReply");
		case cl::Status::TransportError:
			return QStringLiteral("TransportError");
		case cl::Status::CodecError:
			return QStringLiteral("CodecError");
		case cl::Status::BufferTooSmall:
			return QStringLiteral("BufferTooSmall");
		case cl::Status::InvalidArgument:
			return QStringLiteral("InvalidArgument");
		case cl::Status::Busy:
			return QStringLiteral("Busy");
		case cl::Status::RecoveryRequired:
			return QStringLiteral("RecoveryRequired");
		case cl::Status::IdsExhausted:
			return QStringLiteral("IdsExhausted");
	}
	return QStringLiteral("Unknown");
}

template<class T>
bool parseScalar(const QString& text, T& value)
{
	const auto input = text.trimmed().toUtf8();
	if constexpr (std::is_same_v<T, bool>) {
		if (input == "true" || input == "1")
			value = true;
		else if (input == "false" || input == "0")
			value = false;
		else
			return false;
		return true;
	} else {
		const auto parsed =
		    std::from_chars(input.constData(), input.constData() + input.size(), value);
		return parsed.ec == std::errc{} && parsed.ptr == input.constData() + input.size();
	}
}
} // namespace

struct InspectorController::State {
	struct Reply {
		std::vector<std::byte> bytes;
		quint32 session = 0, id = 0, due = 0;
	};

	DemoPeer peer;
	QElapsedTimer elapsed;
	QByteArray descriptor;
	std::vector<rt::RuntimeType> types;
	std::vector<rt::RuntimeMember> members;
	std::vector<rt::RuntimeEnumEntry> enums;
	std::vector<rt::RuntimeCatalog> catalogs;
	std::vector<rt::RuntimeEndpoint> endpoints;
	std::unique_ptr<rt::RuntimeSchemaWorkspace> storage;
	rt::RuntimeSchema schema;
	std::array<std::byte, 65568> tx{}, rx{}, port{}, response{};
	cl::Clock clock{this, +[](void* owner) noexcept {
		                return static_cast<State*>(owner)->now();
	                }};
	telemetry::integration::encoded::EncodedBackend backend{tx, rx, clock};
	Workbench inspector{backend, clock};
	std::vector<Reply> replies;
	std::optional<im::ReadResult> completed;
	std::optional<wb::ApplyResult> applied;
	bool applying = false, mutationAuthorized = false;
	bool demo = false;
	bool serial = false;
	bool remote = false, remoteAccepted = false;
	quint32 remoteRequestId = 0;

	State(QByteArray bytes, bool isDemo, bool writableDemo = false)
	    : peer(writableDemo), descriptor(std::move(bytes)), demo(isDemo)
	{
		elapsed.start();
	}

	quint32 now() const noexcept
	{
		return static_cast<quint32>(elapsed.elapsed());
	}

	cl::Input input() const noexcept
	{
		return {reinterpret_cast<const std::byte*>(descriptor.constData()),
		        static_cast<std::size_t>(descriptor.size())};
	}

	QString initialize(quint32 session, bool admit = true)
	{
		rt::RuntimeSchemaWorkspace sizing{{}, {}, {}, {}, {}};
		const auto required = rt::RuntimeSchema::parse(input(), sizing);
		if (!required && required.status != rt::Status::Capacity)
			return QStringLiteral("Descriptor rejected (status %1, error %2)")
			    .arg(static_cast<unsigned>(required.status))
			    .arg(static_cast<unsigned>(required.error));
		types.resize(required.requirements.types);
		members.resize(required.requirements.members);
		enums.resize(required.requirements.enumEntries);
		catalogs.resize(required.requirements.catalogs);
		endpoints.resize(required.requirements.endpoints);
		storage = std::make_unique<rt::RuntimeSchemaWorkspace>(types, members, enums, catalogs,
		                                                       endpoints);
		const auto parsed = rt::RuntimeSchema::parse(input(), *storage);
		if (!parsed)
			return QStringLiteral("Descriptor rejected (status %1, error %2)")
			    .arg(static_cast<unsigned>(parsed.status))
			    .arg(static_cast<unsigned>(parsed.error));
		schema = parsed.schema;
		return admit ? admitSession(session) : QString{};
	}

	QString admitSession(quint32 session)
	{
		const auto admitted = inspector.admit(schema, input().first(64), session);
		return admitted == cl::Status::Ok ? QString{} : statusName(admitted);
	}

	void closeAndDrain()
	{
		// The port is already closed (the demo releases TX synchronously). Settle
		// only complete, already-owned replies whose delay
		// has elapsed. onReply still enforces the operation deadline. Never pump
		// here: retirement must not authorize another physical handoff. A readback
		// queued by a write callback is closed below before it can be transmitted.
		for (const auto& reply : replies)
			if (cl::expired(now(), reply.due))
				(void)inspector.onReply(reply.bytes, reply.session, reply.id);
		replies.clear();
		inspector.close();
		for (unsigned i = 0; i < 16 && !inspector.drained(); ++i)
			inspector.process(now());
		if (!inspector.drained())
			std::abort();
	}

	~State()
	{
		closeAndDrain();
	}
};

InspectorController::InspectorController(QObject* parent) : QObject(parent)
{
	timer_.setInterval(5);
	connect(&timer_, &QTimer::timeout, this, &InspectorController::pump);
	timer_.start();
}

InspectorController::~InspectorController()
{
	timer_.stop();
	updating_ = true;
	retire();
}

std::optional<bool> InspectorController::retire()
{
	++epoch_;
	values_.clear();
	endpoints_.clear();
	selection_.clear();
	connected_ = false;
	busy_ = false;
	writesEnabled_ = false;
	invalidEditors_.clear();
	connecting_ = false;
	if (transport_) {
		transport_->close();
		if (!transport_->locallyQuiescent())
			std::abort();
		transport_.reset();
	}
	std::optional<bool> applied;
	if (state_) {
		state_->closeAndDrain();
		applied = consumeApply(*state_, false);
		if (state_->remote && state_->inspector.recoveryRequired()) {
			// Local abort releases all Qt-owned TX/RX storage. It does not prove
			// that a peer finished an already handed-off request. Preserve that
			// obligation until the same single-client server admits fresh bootstrap.
			if (retiredState_)
				std::abort();
			retiredSerial_ = state_->serial;
			retiredSerialPort_ = serialPort_;
			retiredSerialBaud_ = serialBaud_;
			retiredPeerHost_ = peerHost_;
			retiredPeerPort_ = peerPort_;
			retiredPeerAddress_ = peerAddress_;
			retiredState_ = std::move(state_);
		}
	}
	state_.reset();
	return applied;
}

std::optional<bool> InspectorController::consumeApply(State& state, bool displaySnapshot)
{
	if (!state.applying || !state.applied)
		return {};
	const auto applied = *state.applied;
	state.applied.reset();
	state.applying = false;
	busy_ = false;
	// The terminal operation result survives retirement of its schema/cache.
	// A readback completed before Disconnect is still a historical confirmation.
	const bool confirmed = applied.writeCompleted && applied.write.succeeded() &&
	                       applied.refreshCompleted && applied.refresh.operation.succeeded() &&
	                       applied.refresh.published;
	const bool publish = confirmed && displaySnapshot && state.inspector.isCurrent();
	if (publish)
		values_.resetSnapshot(state.inspector);
	else
		values_.clear();
	if (confirmed) {
		writeOutcome_ = QStringLiteral("Applied · confirmed by a fresh Field read.");
		status_ = publish
		              ? QStringLiteral("Write applied · displayed values are the device readback.")
		              : QStringLiteral("Write applied · readback completed · no cached values.");
	} else if (applied.write.status == cl::Status::OutcomeUnknown) {
		writeOutcome_ =
		    QStringLiteral("OutcomeUnknown · the write may have executed. It will not be retried.");
		status_ = writeOutcome_;
	} else if (applied.writeCompleted && applied.write.succeeded()) {
		const auto reason =
		    applied.refreshCompleted
		        ? QStringLiteral("%1 · dispatch %2 · endpoint %3 · value %4 · model %5")
		              .arg(statusName(applied.refresh.operation.status))
		              .arg(applied.refresh.operation.dispatch)
		              .arg(applied.refresh.operation.endpointStatus)
		              .arg(static_cast<unsigned>(applied.refresh.valueStatus))
		              .arg(static_cast<unsigned>(applied.refresh.modelStatus))
		        : QStringLiteral("submission %1").arg(statusName(applied.refreshSubmission));
		writeOutcome_ = QStringLiteral("Write applied · readback unconfirmed (%1).").arg(reason);
		status_ = writeOutcome_;
	} else {
		writeOutcome_ = QStringLiteral("Write finished: %1 · dispatch %2 · endpoint %3.")
		                    .arg(statusName(applied.write.status))
		                    .arg(applied.write.dispatch)
		                    .arg(applied.write.endpointStatus);
		status_ = writeOutcome_;
	}
	const auto fatal = [](cl::Status value) {
		return value == cl::Status::TransportError || value == cl::Status::MalformedReply ||
		       value == cl::Status::SessionLost || value == cl::Status::OutcomeUnknown;
	};
	if (displaySnapshot && state.remote && transport_ &&
	    (state.inspector.recoveryRequired() || fatal(applied.write.status) ||
	     (applied.refreshCompleted && fatal(applied.refresh.operation.status)))) {
		transport_->close();
		state.inspector.invalidate();
		connected_ = false;
		endpoints_.clear();
		selection_.clear();
	}
	return confirmed;
}

void InspectorController::notifyState(std::optional<bool> applied, std::optional<bool> read)
{
	// A terminal Apply belongs to the accepted operation, not a connection epoch.
	// Deliver it before stateChanged slots can replace the connection and erase
	// the sole notification. Never access this object after a slot destroys it.
	const auto epoch = epoch_;
	QPointer<InspectorController> alive{this};
	if (applied)
		Q_EMIT applyFinished(*applied);
	if (!alive || epoch_ != epoch)
		return;
	Q_EMIT stateChanged();
	if (alive && epoch_ == epoch && read)
		Q_EMIT readFinished(*read);
}

bool InspectorController::replaceDescriptor(QByteArray bytes, bool demo, bool remote,
                                            bool writableDemo)
{
	if (updating_ || values_.changing() || endpoints_.changing())
		return false;
	if (session_ == std::numeric_limits<quint32>::max()) {
		status_ = QStringLiteral("Session IDs exhausted; restart the application.");
		Q_EMIT stateChanged();
		return false;
	}
	// Validate into a separate generation first. A malformed file does not destroy
	// the current peer, selected field or published snapshot.
	auto candidate = std::make_unique<State>(std::move(bytes), demo, writableDemo);
	candidate->remote = remote;
	candidate->serial = remote && serial_;
	const auto error = candidate->initialize(session_ + 1, !remote);
	if (!error.isEmpty()) {
		status_ = error;
		Q_EMIT stateChanged();
		return false;
	}
	if (remote) {
		if (!transport_ || !transport_->ready())
			return false;
		const auto address = transport_->peerAddress();
		if (retiredState_) {
			if (retiredState_->serial && retiredState_->mutationAuthorized) {
				status_ = QStringLiteral(
				    "The read-only UART bootstrap cannot confirm an earlier write. Application reset proof is required.");
				Q_EMIT stateChanged();
				return false;
			}
			if (serial_ != retiredSerial_ || address != retiredPeerAddress_ ||
			    (serial_ ? serialPort_ != retiredSerialPort_ || serialBaud_ != retiredSerialBaud_
			             : peerHost_ != retiredPeerHost_ || peerPort_ != retiredPeerPort_)) {
				status_ = QStringLiteral("The previous device still requires session cleanup.");
				Q_EMIT stateChanged();
				return false;
			}
			// TCP retires the old socket before accepting another client. The serial
			// demo has one exclusive, sequential host and synchronous read-only
			// dispatch; it cannot process the fresh bootstrap before prior RX and
			// physical TX/COBS leases finish. Both profiles require the complete
			// validated Descriptor transcript from the same peer, never local close
			// or one uncorrelated Resource reply as a remote cleanup proof.
			if (!retiredState_->inspector.confirmQuiescence(
			        {retiredState_->inspector.session(), true, true, true})) {
				status_ = QStringLiteral("Could not confirm the previous device session cleanup.");
				Q_EMIT stateChanged();
				return false;
			}
			retiredState_.reset();
			retiredPeerHost_.clear();
			retiredPeerAddress_.clear();
			retiredPeerPort_ = 0;
			retiredSerialPort_.clear();
			retiredSerialBaud_ = 0;
			retiredSerial_ = false;
		}
		const auto admission = candidate->admitSession(session_ + 1);
		if (!admission.isEmpty()) {
			status_ = admission;
			Q_EMIT stateChanged();
			return false;
		}
		peerAddress_ = address;
	}
	updating_ = true;
	std::optional<bool> retiredApply;
	if (remote) {
		values_.clear();
		endpoints_.clear();
		selection_.clear();
		if (state_) {
			state_->closeAndDrain();
			retiredApply = consumeApply(*state_, false);
			state_.reset();
		}
	} else {
		retiredApply = retire();
	}
	++session_;
	state_ = std::move(candidate);
	endpoints_.rebuild(state_->inspector);
	connected_ = demo || remote;
	writesEnabled_ = demo && writableDemo;
	connecting_ = false;
	status_ =
	    remote && serial_
	        ? QStringLiteral("Serial connected - Descriptor loaded - read-only - COBS/Encoded")
	    : remote
	        ? QStringLiteral("TCP connected · Descriptor loaded · read-only · Encoded transport")
	    : demo && writableDemo
	        ? QStringLiteral("Writable demo connected · explicit Field editing · Encoded transport")
	    : demo ? QStringLiteral("Demo connected · read-only · Encoded transport")
	           : QStringLiteral("Descriptor file · metadata only · no transport connected");
	updating_ = false;
	notifyState(retiredApply);
	return true;
}

QByteArray InspectorController::demoDescriptor() const
{
	DemoPeer peer;
	const auto bytes = peer.descriptor();
	return {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
}

bool InspectorController::connectDemo()
{
	return replaceDescriptor(demoDescriptor(), true);
}

bool InspectorController::connectWritableDemo()
{
	DemoPeer peer{true};
	const auto bytes = peer.descriptor();
	return replaceDescriptor(
	    {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())}, true,
	    false, true);
}

bool InspectorController::connectTcp(const QString& host, quint16 port)
{
	if (updating_ || values_.changing() || endpoints_.changing() || host.trimmed().isEmpty() ||
	    port == 0)
		return false;
	updating_ = true;
	const auto retiredApply = retire();
	auto normalized = host.trimmed().toLower();
	QHostAddress numeric;
	if (numeric.setAddress(normalized))
		normalized = numeric.toString().toLower();
	if (retiredState_ &&
	    (retiredSerial_ || normalized != retiredPeerHost_ || port != retiredPeerPort_)) {
		status_ =
		    QStringLiteral("Reconnect %1:%2 first to finish the previous TCP session cleanup.")
		        .arg(retiredPeerHost_)
		        .arg(retiredPeerPort_);
		updating_ = false;
		notifyState(retiredApply);
		return false;
	}
	serial_ = false;
	peerHost_ = normalized;
	peerPort_ = port;
	connecting_ = true;
	status_ = QStringLiteral("Connecting TCP and loading Descriptor…");
	transport_ = std::make_unique<TcpTransport>();
	auto* tcpPortAdapter = static_cast<TcpTransport*>(transport_.get());
	transport_->setWriteChunkBytes(tcpWriteChunk_);
	bindTransportSignals();
	tcpPortAdapter->connectTo(peerHost_, peerPort_);
	updating_ = false;
	notifyState(retiredApply);
	return true;
}

void InspectorController::bindTransportSignals()
{
	const auto epoch = epoch_;
	connect(
	    transport_.get(), &DeviceTransport::descriptorReceived, this,
	    [this, epoch](QByteArray bytes) {
		    transportDescriptor(epoch, std::move(bytes));
	    },
	    Qt::QueuedConnection);
	connect(
	    transport_.get(), &DeviceTransport::packetAccepted, this,
	    [this, epoch](quint32 session, quint32 id) {
		    if (epoch == epoch_ && state_ && state_->remote &&
		        session == state_->inspector.session() && id == state_->remoteRequestId)
			    state_->remoteAccepted = true;
	    },
	    Qt::QueuedConnection);
	connect(
	    transport_.get(), &DeviceTransport::packetReceived, this,
	    [this, epoch](const QByteArray& bytes, quint32 session, quint32 id) {
		    if (epoch != epoch_ || !state_ || !state_->remote ||
		        session != state_->inspector.session() || id != state_->remoteRequestId ||
		        !state_->replies.empty())
			    return;
		    const auto* first = reinterpret_cast<const std::byte*>(bytes.constData());
		    state_->replies.push_back({{first, first + bytes.size()}, session, id, state_->now()});
	    },
	    Qt::QueuedConnection);
	connect(
	    transport_.get(), &DeviceTransport::transportError, this,
	    [this, epoch](const QString& error) {
		    transportFailed(epoch, error);
	    },
	    Qt::QueuedConnection);
}

bool InspectorController::serialAvailable() const noexcept
{
#ifdef INSPECTOR_ENABLE_SERIAL
	return true;
#else
	return false;
#endif
}

QStringList InspectorController::serialPorts() const
{
	QStringList ports;
#ifdef INSPECTOR_ENABLE_SERIAL
	for (const auto& port : QSerialPortInfo::availablePorts())
		ports.push_back(port.systemLocation());
#endif
	return ports;
}

bool InspectorController::connectSerial(const QString& port, qint32 baud)
{
#ifndef INSPECTOR_ENABLE_SERIAL
	Q_UNUSED(port);
	Q_UNUSED(baud);
	status_ = QStringLiteral("Build with INSPECTOR_ENABLE_SERIAL and COBS_ROOT to enable serial.");
	Q_EMIT stateChanged();
	return false;
#else
	if (updating_ || values_.changing() || endpoints_.changing() || port.trimmed().isEmpty() ||
	    baud < qint32(inspector_serial::minBaudRate) ||
	    baud > qint32(inspector_serial::maxBaudRate))
		return false;
	updating_ = true;
	const auto retiredApply = retire();
	const auto name = port.trimmed();
	if (retiredState_ &&
	    (!retiredSerial_ || name != retiredSerialPort_ || baud != retiredSerialBaud_)) {
		status_ = QStringLiteral("Reconnect the previous device first to finish session cleanup.");
		updating_ = false;
		notifyState(retiredApply);
		return false;
	}
	serial_ = true;
	serialPort_ = name;
	serialBaud_ = baud;
	connecting_ = true;
	status_ = QStringLiteral("Opening serial port and loading Descriptor...");
	transport_ = std::make_unique<SerialTransport>();
	transport_->setWriteChunkBytes(serialWriteChunk_);
	auto* serial = static_cast<SerialTransport*>(transport_.get());
	bindTransportSignals();
	serial->connectTo(name, baud);
	updating_ = false;
	notifyState(retiredApply);
	return true;
#endif
}

void InspectorController::setSerialWriteChunkBytes(int bytes) noexcept
{
	serialWriteChunk_ = std::max(0, bytes);
	if (serial_ && transport_)
		transport_->setWriteChunkBytes(serialWriteChunk_);
}

void InspectorController::transportDescriptor(quint64 epoch, QByteArray bytes)
{
	if (epoch != epoch_ || !transport_)
		return;
	if (updating_ || values_.changing() || endpoints_.changing()) {
		QTimer::singleShot(0, this, [this, epoch, bytes = std::move(bytes)] {
			transportDescriptor(epoch, bytes);
		});
		return;
	}
	QPointer<InspectorController> alive{this};
	const bool admitted = replaceDescriptor(std::move(bytes), false, true);
	if (alive && epoch == epoch_ && !admitted)
		transportFailed(epoch, status_);
}

void InspectorController::transportFailed(quint64 epoch, QString error)
{
	if (epoch != epoch_)
		return;
	if (updating_ || values_.changing() || endpoints_.changing()) {
		QTimer::singleShot(0, this, [this, epoch, error = std::move(error)] {
			transportFailed(epoch, error);
		});
		return;
	}
	// The transport preserves complete packets received before EOF. Apply them
	// before closing the logical operation; onReply/process still enforce its
	// deadline. The socket is already closed, so pump cannot authorize new TX.
	if (state_ && state_->remote && !state_->applying && !state_->replies.empty()) {
		QPointer<InspectorController> alive{this};
		pump();
		if (!alive || epoch != epoch_)
			return;
	}
	const bool readPending = busy_ && state_ && !state_->applying;
	updating_ = true;
	const auto applied = retire();
	status_ = (serial_ ? QStringLiteral("Serial: %1") : QStringLiteral("TCP: %1")).arg(error);
	updating_ = false;
	notifyState(applied, readPending ? std::optional<bool>{false} : std::nullopt);
}

bool InspectorController::canRefresh() const noexcept
{
	return connected_ && !busy_ && !editing() && state_ &&
	       state_->inspector.snapshotInfo().selected;
}

bool InspectorController::writeProfileSupported() const noexcept
{
	return connected_ && state_ && !state_->serial && (state_->remote || state_->demo);
}

bool InspectorController::editing() const noexcept
{
	return state_ && state_->inspector.editing();
}

bool InspectorController::canEdit() const noexcept
{
	return writeProfileSupported() && writesEnabled_ && !busy_ && state_->inspector.canEdit();
}

bool InspectorController::canApply() const noexcept
{
	return writeProfileSupported() && writesEnabled_ && !busy_ && invalidEditors_.isEmpty() &&
	       state_->inspector.canApply();
}

void InspectorController::setWritesEnabled(bool enabled)
{
	if (updating_ || values_.changing() || endpoints_.changing() || busy_)
		return;
	if (enabled && !writeProfileSupported()) {
		status_ = QStringLiteral(
		    "This transport profile supports read-only inspection. Descriptor write capability is still shown.");
		Q_EMIT stateChanged();
		return;
	}
	updating_ = true;
	if (!enabled && editing()) {
		state_->inspector.cancelEdit();
		invalidEditors_.clear();
		values_.resetSnapshot(state_->inspector);
	}
	writesEnabled_ = enabled;
	updating_ = false;
	Q_EMIT stateChanged();
}

bool InspectorController::beginEdit()
{
	if (updating_ || values_.changing() || endpoints_.changing() || !canEdit())
		return false;
	const auto status = state_->inspector.beginEdit();
	if (status != rt::Status::Ok) {
		status_ = QStringLiteral("Could not start editing (runtime %1).")
		              .arg(static_cast<unsigned>(status));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	invalidEditors_.clear();
	values_.resetSnapshot(state_->inspector);
	status_ = QStringLiteral(
	    "Editing a local draft · Apply writes once and reads back the actual Field.");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

void InspectorController::cancelEdit()
{
	if (updating_ || values_.changing() || endpoints_.changing() || !editing() || busy_)
		return;
	updating_ = true;
	state_->inspector.cancelEdit();
	invalidEditors_.clear();
	values_.resetSnapshot(state_->inspector);
	status_ = QStringLiteral("Draft discarded · device was not changed.");
	updating_ = false;
	Q_EMIT stateChanged();
}

bool InspectorController::setScalarText(const QModelIndex& index, const QString& text)
{
	if (updating_ || values_.changing() || endpoints_.changing() || !editing() || busy_)
		return false;
	const auto node = values_.node(index);
	if (!node)
		return false;
	const auto info = state_->inspector.info(node.value);
	if (!info || (info.value.kind != telemetry::TypeKind::Scalar &&
	              info.value.kind != telemetry::TypeKind::Enum))
		return false;
	// Focusing and leaving a field is not an edit. In particular the displayed
	// NaN label does not encode its sign/payload/quiet bit, so never reparse it
	// unless the operator actually changes the text.
	if (text == values_.data(index, ValueModel::EditorTextRole).toString()) {
		invalidEditors_.remove(index.internalId());
		status_ = QStringLiteral("Draft value unchanged · original wire bits retained.");
		Q_EMIT stateChanged();
		return true;
	}
	rt::Status status = rt::Status::TypeMismatch;
	bool parsed = false;
	const auto set = [&]<class T>() {
		T number{};
		if ((parsed = parseScalar(text, number)))
			status = state_->inspector.setScalar(node.value, number);
	};
	using Code = telemetry::ScalarCode;
	switch (info.value.code) {
		case Code::Bool:
			set.template operator()<bool>();
			break;
		case Code::U8:
			set.template operator()<std::uint8_t>();
			break;
		case Code::S8:
			set.template operator()<std::int8_t>();
			break;
		case Code::U16:
			set.template operator()<std::uint16_t>();
			break;
		case Code::S16:
			set.template operator()<std::int16_t>();
			break;
		case Code::U32:
			set.template operator()<std::uint32_t>();
			break;
		case Code::S32:
			set.template operator()<std::int32_t>();
			break;
		case Code::U64:
			set.template operator()<std::uint64_t>();
			break;
		case Code::S64:
			set.template operator()<std::int64_t>();
			break;
		case Code::F32:
			set.template operator()<float>();
			break;
		case Code::F64:
			set.template operator()<double>();
			break;
	}
	if (!parsed || status != rt::Status::Ok) {
		invalidEditors_.insert(index.internalId());
		status_ =
		    parsed ? QStringLiteral("Draft edit refused (runtime %1).")
		                 .arg(static_cast<unsigned>(status))
		           : QStringLiteral("Invalid value for the exact scalar type · draft unchanged.");
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	invalidEditors_.remove(index.internalId());
	values_.updateScalar(index);
	status_ = QStringLiteral("Draft changed · not yet written to the device.");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::apply()
{
	if (updating_ || values_.changing() || endpoints_.changing() || !canApply())
		return false;
	const auto submitted = state_->inspector.apply(
	    [state = state_.get()](const wb::ApplyResult& result) noexcept {
		    state->applied = result;
	    },
	    static_cast<quint32>(readTimeout_), static_cast<quint32>(readTimeout_));
	if (!submitted) {
		status_ = QStringLiteral("Apply refused: %1.").arg(statusName(submitted.status));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	state_->applying = true;
	busy_ = true;
	values_.clear();
	writeOutcome_ = QStringLiteral("Pending · waiting for write result and readback.");
	status_ = QStringLiteral("Writing once, then refreshing from the device…");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::loadDescriptor(const QUrl& path)
{
	if (updating_ || values_.changing() || endpoints_.changing())
		return false;
	QFile file(path.isLocalFile() ? path.toLocalFile() : path.toString());
	if (!file.open(QIODevice::ReadOnly)) {
		status_ = QStringLiteral("Cannot open Descriptor file: %1").arg(file.errorString());
		Q_EMIT stateChanged();
		return false;
	}
	if (file.size() < 64 || file.size() > telemetry::Limits::maxDescriptorBytes) {
		status_ = QStringLiteral("Descriptor file size is outside the supported bounds.");
		Q_EMIT stateChanged();
		return false;
	}
	const auto expected = file.size();
	const auto bytes = file.readAll();
	if (bytes.size() != expected) {
		status_ = QStringLiteral("Could not read the complete Descriptor file.");
		Q_EMIT stateChanged();
		return false;
	}
	return replaceDescriptor(bytes, false);
}

bool InspectorController::openDescriptorPath(const QString& path)
{
	return loadDescriptor(QUrl::fromLocalFile(QDir::fromNativeSeparators(path)));
}

bool InspectorController::selectEndpoint(const QModelIndex& index)
{
	if (updating_ || values_.changing() || endpoints_.changing() || !state_ || editing() ||
	    state_->applying)
		return false;
	const auto endpoint = endpoints_.endpoint(index);
	if (endpoint.isEmpty())
		return false;
	if (endpoint.value(QStringLiteral("category")).toInt() != 1 ||
	    !endpoint.value(QStringLiteral("readable")).toBool()) {
		updating_ = true;
		state_->inspector.clearSelection();
		values_.clear();
		selection_ = endpoint.value(QStringLiteral("path")).toString();
		status_ = QStringLiteral("Metadata only · this endpoint cannot be read as a Field.");
		updating_ = false;
		Q_EMIT stateChanged();
		return true;
	}
	const auto binding = state_->inspector.bindField(endpoint.value(QStringLiteral("id")).toUInt());
	if (!binding || state_->inspector.selectField(binding.endpoint) != cl::Status::Ok)
		return false;
	updating_ = true;
	values_.clear();
	selection_ = endpoint.value(QStringLiteral("path")).toString();
	status_ = connected_ ? QStringLiteral("Field selected · press Refresh to read it")
	                     : QStringLiteral("Field metadata · no transport connected");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

bool InspectorController::refresh()
{
	if (updating_ || values_.changing() || endpoints_.changing() || !state_ || !connected_ ||
	    busy_ || editing())
		return false;
	auto readTimeout = static_cast<quint32>(readTimeout_);
#ifdef INSPECTOR_ENABLE_SERIAL
	if (state_->serial) {
		const auto* type = state_->schema.type(state_->inspector.snapshotInfo().type);
		if (!type || type->wireBytes > inspector_serial::maxBodyBytes - 32u) {
			status_ = QStringLiteral("Selected Field exceeds the serial frame payload limit.");
			Q_EMIT stateChanged();
			return false;
		}
		if (!readTimeoutExplicit_)
			readTimeout = inspector_serial::exchangeTimeoutMs(
			    32u, 32u + type->wireBytes, static_cast<std::uint32_t>(serialBaud_), 3000);
	}
#endif
	const auto submitted = state_->inspector.refresh(
	    [state = state_.get()](const im::ReadResult& result) noexcept {
		    // Never emit Qt signals inside a core callback: slots may replace the
		    // session or destroy this controller. Publish after returning from core.
		    state->completed = result;
	    },
	    readTimeout);
	if (!submitted) {
		status_ = QStringLiteral("Read refused: %1 (model %2)")
		              .arg(statusName(submitted.status))
		              .arg(static_cast<unsigned>(submitted.modelStatus));
		Q_EMIT stateChanged();
		return false;
	}
	updating_ = true;
	values_.clear();
	busy_ = true;
	status_ = QStringLiteral("Reading…");
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

void InspectorController::pump()
{
	if (updating_ || values_.changing() || endpoints_.changing() || !state_)
		return;
	updating_ = true;
	auto& state = *state_;
	const auto now = state.now();
	state.inspector.process(now);
	if (state.remoteAccepted) {
		state.remoteAccepted = false;
		(void)state.inspector.onSendAccepted(state.inspector.session(), state.remoteRequestId);
	}
	const auto packet = state.inspector.nextPacket();
	if (state.remote && transport_ && packet && transport_->canStage() &&
	    transport_->stage(packet)) {
		if (state.inspector.prepareHandoff(packet.session, packet.requestId)) {
			if (mutationPacket(packet))
				state.mutationAuthorized = true;
			state.remoteRequestId = packet.requestId;
			if (!transport_->sendStaged())
				state.inspector.invalidate();
		} else {
			transport_->discardStaged();
		}
	}
	if (state.demo && packet && packet.bytes.size() <= state.port.size()) {
		std::copy(packet.bytes.begin(), packet.bytes.end(), state.port.begin());
		if (state.inspector.prepareHandoff(packet.session, packet.requestId) &&
		    state.inspector.onSendAccepted(packet.session, packet.requestId)) {
			if (mutationPacket(packet))
				state.mutationAuthorized = true;
			const auto written = state.peer.dispatch(
			    cl::Input{state.port}.first(packet.bytes.size()), state.response, packet.session);
			if (written && !dropReplies_)
				state.replies.push_back({{state.response.begin(), state.response.begin() + written},
				                         packet.session,
				                         packet.requestId,
				                         now + static_cast<quint32>(replyDelay_)});
		}
	}
	for (auto i = state.replies.begin(); i != state.replies.end();) {
		if (!cl::expired(now, i->due)) {
			++i;
			continue;
		}
		(void)state.inspector.onReply(i->bytes, i->session, i->id);
		i = state.replies.erase(i);
	}
	state.inspector.process(now);
	const auto completion = state.completed;
	state.completed.reset();
	const bool stale = !state.inspector.isCurrent();
	bool notify = false, published = false;
	if (stale) {
		if (connected_ || !selection_.isEmpty() || endpoints_.rowCount()) {
			values_.clear();
			endpoints_.clear();
			selection_.clear();
			connected_ = false;
			status_ = QStringLiteral("Schema or session changed · reconnect to read again");
			notify = true;
		}
	}
	if (completion) {
		busy_ = false;
		published = !stale && completion->published && completion->operation.succeeded();
		if (published) {
			values_.resetSnapshot(state.inspector);
			status_ = QStringLiteral("Read complete · %1 bytes")
			              .arg(state.inspector.snapshotInfo().bytes);
		} else if (!stale) {
			status_ = QStringLiteral("Read finished: %1 · dispatch %2 · endpoint %3 · value %4")
			              .arg(statusName(completion->operation.status))
			              .arg(completion->operation.dispatch)
			              .arg(completion->operation.endpointStatus)
			              .arg(static_cast<unsigned>(completion->valueStatus));
		}
		const auto outcome = completion->operation.status;
		const bool fatalTransport =
		    outcome == cl::Status::TransportError || outcome == cl::Status::MalformedReply ||
		    outcome == cl::Status::SessionLost || outcome == cl::Status::OutcomeUnknown;
		if (state.remote && transport_ && (state.inspector.recoveryRequired() || fatalTransport)) {
			transport_->close();
			state.inspector.invalidate();
			connected_ = false;
			values_.clear();
			endpoints_.clear();
			selection_.clear();
		}
		notify = true;
	}
	const auto applied = consumeApply(state, true);
	notify = notify || applied.has_value();
	// All backend calls have returned before application signals can run slots.
	updating_ = false;
	if (notify)
		notifyState(applied, completion ? std::optional<bool>{published} : std::nullopt);
}

void InspectorController::disconnectDevice()
{
	if (updating_ || values_.changing() || endpoints_.changing())
		return;
	updating_ = true;
	const auto applied = retire();
	status_ = QStringLiteral("Disconnected · no cached values");
	updating_ = false;
	notifyState(applied);
}

void InspectorController::setReplyDelay(int milliseconds) noexcept
{
	replyDelay_ = std::clamp(milliseconds, 0, 60000);
}

void InspectorController::setDropReplies(bool drop) noexcept
{
	dropReplies_ = drop;
}

void InspectorController::setRejectWrites(bool reject) noexcept
{
	if (state_ && state_->demo)
		state_->peer.setRejectWrites(reject);
}

quint64 InspectorController::demoWriteCount() const noexcept
{
	return state_ && state_->demo ? state_->peer.writeCount() : 0;
}

void InspectorController::setReadTimeout(int milliseconds) noexcept
{
	readTimeout_ = std::clamp(milliseconds, 1, 60000);
	readTimeoutExplicit_ = true;
}

void InspectorController::setTcpWriteChunkBytes(int bytes) noexcept
{
	tcpWriteChunk_ = std::max(0, bytes);
	if (!serial_ && transport_)
		transport_->setWriteChunkBytes(tcpWriteChunk_);
}

Workbench& InspectorController::inspector() noexcept
{
	if (!state_)
		std::abort();
	return state_->inspector;
}

} // namespace inspector_qml
