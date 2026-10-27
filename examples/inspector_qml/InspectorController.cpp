/*
 * @file InspectorController.cpp
 * @brief Own descriptor generations and drive the real Encoded peer from the Qt event loop.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "DemoPeer.hpp"
#include "TcpTransport.hpp"
#include <integration/encoded/Backend.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtNetwork/QHostAddress>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace inspector_qml {
namespace cl = telemetry::client;
namespace rt = telemetry::runtime;
namespace im = telemetry::integration::inspector;

namespace {
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
	Inspector inspector{backend, clock};
	std::vector<Reply> replies;
	std::optional<im::ReadResult> completed;
	bool demo = false;
	bool tcp = false, tcpAccepted = false;
	quint32 tcpRequestId = 0;

	State(QByteArray bytes, bool isDemo) : descriptor(std::move(bytes)), demo(isDemo)
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
		// The local dispatcher releases TX synchronously. Discard every deferred RX
		// copy, then complete logical callbacks before freeing schema/index storage.
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

void InspectorController::retire()
{
	++epoch_;
	values_.clear();
	endpoints_.clear();
	selection_.clear();
	connected_ = false;
	busy_ = false;
	connecting_ = false;
	if (tcp_) {
		tcp_->close();
		if (!tcp_->locallyQuiescent())
			std::abort();
		tcp_.reset();
	}
	if (state_ && state_->tcp) {
		state_->closeAndDrain();
		if (state_->inspector.recoveryRequired()) {
			// Local abort releases all Qt-owned TX/RX storage. It does not prove
			// that a peer finished an already handed-off request. Preserve that
			// obligation until the same single-client server admits fresh bootstrap.
			if (retiredState_)
				std::abort();
			retiredPeerHost_ = peerHost_;
			retiredPeerPort_ = peerPort_;
			retiredPeerAddress_ = peerAddress_;
			retiredState_ = std::move(state_);
		}
	}
	state_.reset();
}

bool InspectorController::replaceDescriptor(QByteArray bytes, bool demo, bool tcp)
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
	auto candidate = std::make_unique<State>(std::move(bytes), demo);
	candidate->tcp = tcp;
	const auto error = candidate->initialize(session_ + 1, !tcp);
	if (!error.isEmpty()) {
		status_ = error;
		Q_EMIT stateChanged();
		return false;
	}
	if (tcp) {
		if (!tcp_ || !tcp_->ready())
			return false;
		const auto address = tcp_->peerAddress();
		if (retiredState_) {
			if (peerHost_ != retiredPeerHost_ || peerPort_ != retiredPeerPort_ ||
			    address != retiredPeerAddress_) {
				status_ = QStringLiteral("The previous TCP peer still requires session cleanup.");
				Q_EMIT stateChanged();
				return false;
			}
			// Supported server profile: one active connection, synchronous dispatch,
			// old socket/parser fully retired before accepting a new connection.
			// A fresh successful Descriptor bootstrap from the same actual peer is
			// therefore the remote proof; local close alone was deliberately not.
			if (!retiredState_->inspector.confirmQuiescence(
			        {retiredState_->inspector.session(), true, true, true})) {
				status_ = QStringLiteral("Could not confirm the previous TCP session cleanup.");
				Q_EMIT stateChanged();
				return false;
			}
			retiredState_.reset();
			retiredPeerHost_.clear();
			retiredPeerAddress_.clear();
			retiredPeerPort_ = 0;
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
	if (tcp) {
		values_.clear();
		endpoints_.clear();
		selection_.clear();
		state_.reset();
	} else {
		retire();
	}
	++session_;
	state_ = std::move(candidate);
	endpoints_.rebuild(state_->inspector);
	connected_ = demo || tcp;
	connecting_ = false;
	status_ =
	    tcp    ? QStringLiteral("TCP connected · Descriptor loaded · read-only · Encoded transport")
	    : demo ? QStringLiteral("Demo connected · read-only · Encoded transport")
	           : QStringLiteral("Descriptor file · metadata only · no transport connected");
	updating_ = false;
	Q_EMIT stateChanged();
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

bool InspectorController::connectTcp(const QString& host, quint16 port)
{
	if (updating_ || values_.changing() || endpoints_.changing() || host.trimmed().isEmpty() ||
	    port == 0)
		return false;
	updating_ = true;
	retire();
	auto normalized = host.trimmed().toLower();
	QHostAddress numeric;
	if (numeric.setAddress(normalized))
		normalized = numeric.toString().toLower();
	if (retiredState_ && (normalized != retiredPeerHost_ || port != retiredPeerPort_)) {
		status_ =
		    QStringLiteral("Reconnect %1:%2 first to finish the previous TCP session cleanup.")
		        .arg(retiredPeerHost_)
		        .arg(retiredPeerPort_);
		updating_ = false;
		Q_EMIT stateChanged();
		return false;
	}
	peerHost_ = normalized;
	peerPort_ = port;
	connecting_ = true;
	status_ = QStringLiteral("Connecting TCP and loading Descriptor…");
	tcp_ = std::make_unique<TcpTransport>();
	tcp_->setWriteChunkBytes(tcpWriteChunk_);
	const auto epoch = epoch_;
	connect(
	    tcp_.get(), &TcpTransport::descriptorReceived, this,
	    [this, epoch](QByteArray bytes) {
		    tcpDescriptor(epoch, std::move(bytes));
	    },
	    Qt::QueuedConnection);
	connect(
	    tcp_.get(), &TcpTransport::packetAccepted, this,
	    [this, epoch](quint32 session, quint32 id) {
		    if (epoch == epoch_ && state_ && state_->tcp &&
		        session == state_->inspector.session() && id == state_->tcpRequestId)
			    state_->tcpAccepted = true;
	    },
	    Qt::QueuedConnection);
	connect(
	    tcp_.get(), &TcpTransport::packetReceived, this,
	    [this, epoch](const QByteArray& bytes, quint32 session, quint32 id) {
		    if (epoch != epoch_ || !state_ || !state_->tcp ||
		        session != state_->inspector.session() || id != state_->tcpRequestId ||
		        !state_->replies.empty())
			    return;
		    const auto* first = reinterpret_cast<const std::byte*>(bytes.constData());
		    state_->replies.push_back({{first, first + bytes.size()}, session, id, state_->now()});
	    },
	    Qt::QueuedConnection);
	connect(
	    tcp_.get(), &TcpTransport::transportError, this,
	    [this, epoch](const QString& error) {
		    tcpFailed(epoch, error);
	    },
	    Qt::QueuedConnection);
	tcp_->connectTo(peerHost_, peerPort_);
	updating_ = false;
	Q_EMIT stateChanged();
	return true;
}

void InspectorController::tcpDescriptor(quint64 epoch, QByteArray bytes)
{
	if (epoch != epoch_ || !tcp_)
		return;
	if (updating_ || values_.changing() || endpoints_.changing()) {
		QTimer::singleShot(0, this, [this, epoch, bytes = std::move(bytes)] {
			tcpDescriptor(epoch, bytes);
		});
		return;
	}
	QPointer<InspectorController> alive{this};
	const bool admitted = replaceDescriptor(std::move(bytes), false, true);
	if (alive && epoch == epoch_ && !admitted)
		tcpFailed(epoch, status_);
}

void InspectorController::tcpFailed(quint64 epoch, QString error)
{
	if (epoch != epoch_)
		return;
	if (updating_ || values_.changing() || endpoints_.changing()) {
		QTimer::singleShot(0, this, [this, epoch, error = std::move(error)] {
			tcpFailed(epoch, error);
		});
		return;
	}
	// The transport preserves complete packets received before EOF. Apply them
	// before closing the logical operation; onReply/process still enforce its
	// deadline. The socket is already closed, so pump cannot authorize new TX.
	if (state_ && state_->tcp && !state_->replies.empty()) {
		QPointer<InspectorController> alive{this};
		pump();
		if (!alive || epoch != epoch_)
			return;
	}
	const bool pending = busy_;
	updating_ = true;
	retire();
	status_ = QStringLiteral("TCP: %1").arg(error);
	updating_ = false;
	const auto retiredEpoch = epoch_;
	QPointer<InspectorController> alive{this};
	Q_EMIT stateChanged();
	if (alive && epoch_ == retiredEpoch && pending)
		Q_EMIT readFinished(false);
}

bool InspectorController::canRefresh() const noexcept
{
	return connected_ && !busy_ && state_ && state_->inspector.snapshotInfo().selected;
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
	if (updating_ || values_.changing() || endpoints_.changing() || !state_)
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
	if (updating_ || values_.changing() || endpoints_.changing() || !state_ || !connected_ || busy_)
		return false;
	const auto submitted = state_->inspector.refresh(
	    [state = state_.get()](const im::ReadResult& result) noexcept {
		    // Never emit Qt signals inside a core callback: slots may replace the
		    // session or destroy this controller. Publish after returning from core.
		    state->completed = result;
	    },
	    static_cast<quint32>(readTimeout_));
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
	const auto epoch = epoch_;
	const auto now = state.now();
	state.inspector.process(now);
	if (state.tcpAccepted) {
		state.tcpAccepted = false;
		(void)state.inspector.onSendAccepted(state.inspector.session(), state.tcpRequestId);
	}
	const auto packet = state.inspector.nextPacket();
	if (state.tcp && tcp_ && packet && tcp_->canStage() && tcp_->stage(packet)) {
		if (state.inspector.prepareHandoff(packet.session, packet.requestId)) {
			state.tcpRequestId = packet.requestId;
			if (!tcp_->sendStaged())
				state.inspector.invalidate();
		} else {
			tcp_->discardStaged();
		}
	}
	if (state.demo && packet && packet.bytes.size() <= state.port.size()) {
		std::copy(packet.bytes.begin(), packet.bytes.end(), state.port.begin());
		if (state.inspector.prepareHandoff(packet.session, packet.requestId) &&
		    state.inspector.onSendAccepted(packet.session, packet.requestId)) {
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
		if (state.tcp && tcp_ && (state.inspector.recoveryRequired() || fatalTransport)) {
			tcp_->close();
			state.inspector.invalidate();
			connected_ = false;
			values_.clear();
			endpoints_.clear();
			selection_.clear();
		}
		notify = true;
	}
	// All backend calls have returned before application signals can run slots.
	updating_ = false;
	QPointer<InspectorController> alive{this};
	if (notify)
		Q_EMIT stateChanged();
	if (alive && epoch_ == epoch && completion)
		Q_EMIT readFinished(published);
}

void InspectorController::disconnectDevice()
{
	if (updating_ || values_.changing() || endpoints_.changing())
		return;
	updating_ = true;
	retire();
	status_ = QStringLiteral("Disconnected · no cached values");
	updating_ = false;
	Q_EMIT stateChanged();
}

void InspectorController::setReplyDelay(int milliseconds) noexcept
{
	replyDelay_ = std::clamp(milliseconds, 0, 60000);
}

void InspectorController::setDropReplies(bool drop) noexcept
{
	dropReplies_ = drop;
}

void InspectorController::setReadTimeout(int milliseconds) noexcept
{
	readTimeout_ = std::clamp(milliseconds, 1, 60000);
}

void InspectorController::setTcpWriteChunkBytes(int bytes) noexcept
{
	tcpWriteChunk_ = std::max(0, bytes);
	if (tcp_)
		tcp_->setWriteChunkBytes(tcpWriteChunk_);
}

Inspector& InspectorController::inspector() noexcept
{
	if (!state_)
		std::abort();
	return state_->inspector;
}

} // namespace inspector_qml
