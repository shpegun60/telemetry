/*
 * @file SerialTransport.cpp
 * @brief Drive the existing COBS SerialAdapter with bounded ordinary Resource and TEQ1 bodies.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "SerialTransport.hpp"
#include "DescriptorBootstrap.hpp"
#include "../inspector_serial/SerialProfile.hpp"
#include <adapters/qt/SerialAdapter.h>
#include <cobs/Cobs.h>
#include <wire/EncodedWire.hpp>
#include <QtCore/QTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace inspector_qml {
namespace {
using Link = cobs::Endpoint<wire::Pool<2, 2>, inspector_serial::FrameFormat>;
namespace encoded = telemetry::protocol::encoded;
using Input = telemetry::client::Input;

Input input(const QByteArray& bytes) noexcept
{
	return {reinterpret_cast<const std::byte*>(bytes.constData()),
	        static_cast<std::size_t>(bytes.size())};
}

// The existing adapter treats a short write as a failed frame, clears pending
// output, and never retries its prefix. This narrow port stand-in lets tests
// force the same Qt short-acceptance boundary on Qt versions before 6.10.
class Port final : public QSerialPort {
public:
	int writeLimit = std::numeric_limits<int>::max();
	bool atDelimiter = true;

	QByteArray readAll()
	{
		auto bytes = QSerialPort::readAll();
		if (!bytes.isEmpty())
			atDelimiter = bytes.back() == '\0';
		return bytes;
	}

	qint64 write(const char* bytes, qint64 count)
	{
		return QSerialPort::write(bytes, std::min(count, qint64(writeLimit)));
	}
};

using Adapter = adapters::qt::SerialAdapter<Link, Port>;
} // namespace

struct SerialTransport::Impl {
	SerialTransport& owner;
	Port port;
	QTimer timeout, discoveryTimeout, serviceTimer;
	DescriptorBootstrap discovery;
	QString identity;
	std::unique_ptr<Link> link;
	std::unique_ptr<Adapter> adapter;
	std::optional<Link::Message> stagedMessage;
	quint64 generation = 0;
	quint32 session = 0, requestId = 0;
	int timeoutMs = 5000;
	bool open = false, admitted = false, awaiting = false, handling = false;

	template<class Callback>
	void later(int milliseconds, Callback callback)
	{
		const auto epoch = generation;
		QTimer::singleShot(milliseconds, &owner,
		                   [this, epoch, callback = std::move(callback)]() mutable {
			                   if (epoch == generation)
				                   callback();
		                   });
	}

	explicit Impl(SerialTransport& target) : owner(target)
	{
		timeout.setSingleShot(true);
		discoveryTimeout.setSingleShot(true);
		serviceTimer.setInterval(5);
		port.setReadBufferSize(4 * qint64(inspector_serial::maxBodyBytes));
		QObject::connect(&timeout, &QTimer::timeout, &owner, [this] {
			fail(QStringLiteral("Serial exchange timed out"));
		});
		QObject::connect(&discoveryTimeout, &QTimer::timeout, &owner, [this] {
			fail(QStringLiteral("Serial Descriptor discovery timed out"));
		});
		QObject::connect(&serviceTimer, &QTimer::timeout, &owner, [this] {
			service();
		});
	}

	void close(bool invalidatePosted = true)
	{
		if (invalidatePosted)
			++generation;
		open = admitted = awaiting = false;
		timeout.stop();
		discoveryTimeout.stop();
		serviceTimer.stop();
		// Every Message/Packet is returned while its Endpoint still exists. The
		// adapter releases the active TX lease; Qt has always owned its own copy.
		stagedMessage.reset();
		if (adapter) {
			adapter->set_service_handler({});
			adapter->abort_outgoing();
			(void)adapter->unbind();
			adapter.reset();
		}
		port.close();
		if (link) {
			while (link->has_packet()) {
				auto packet = link->pop_packet();
			}
			link.reset();
		}
	}

	void fail(QString reason)
	{
		if (!open)
			return;
		// As for TCP, preserve complete frames queued before a driver error;
		// explicit close/reconnect invalidates their generation instead.
		close(false);
		auto* target = &owner;
		later(0, [target, reason = std::move(reason)] {
			Q_EMIT target->transportError(reason);
		});
	}

	void scheduleService()
	{
		later(0, [this] {
			service();
		});
	}

	bool sendMessage(Link::Message& message)
	{
		const auto result = link->send(message);
		// Failure retains an encoded Message. Release it before a caller can
		// close the adapter/Endpoint; an automatic retry would duplicate bytes.
		message = {};
		return result == wire::SendResult::Sent &&
		       adapter->take_transport_error() == adapters::qt::TransportError::None;
	}

	void bootstrap()
	{
		if (!open || admitted || awaiting)
			return;
		if (link->tx_active() || port.bytesToWrite() != 0) {
			later(1, [this] {
				bootstrap();
			});
			return;
		}
		if (link->has_packet() || port.bytesAvailable() != 0 || !port.atDelimiter) {
			fail(QStringLiteral("Unsolicited serial Resource reply"));
			return;
		}
		std::array<std::byte, 16> bytes{};
		const auto built = discovery.request(bytes);
		if (built.status != resource::protocol::client::BuildStatus::Ok) {
			fail(QStringLiteral("Cannot build serial Resource request"));
			return;
		}
		bool sent = false;
		{
			auto message = link->make_message(built.written);
			if (message &&
			    message.append_bytes(std::span<const std::uint8_t>{
			        reinterpret_cast<const std::uint8_t*>(bytes.data()), built.written})) {
				awaiting = true;
				timeout.start(timeoutMs);
				sent = sendMessage(message);
			}
		}
		if (!sent)
			fail(QStringLiteral("Serial Resource transmission refused or incomplete"));
	}

	void received(QByteArray bytes)
	{
		if (admitted) {
			const auto parsed = encoded::parse(input(bytes));
			if (!parsed || !parsed.header.reply) {
				fail(QStringLiteral("Invalid serial Encoded reply"));
				return;
			}
			const auto header = parsed.header;
			if (awaiting && header.session == session && header.requestId == requestId) {
				awaiting = false;
				timeout.stop();
			}
			auto* target = &owner;
			later(0, [target, bytes = std::move(bytes), header] {
				Q_EMIT target->packetReceived(bytes, header.session, header.requestId);
			});
			return;
		}
		if (!awaiting || link->has_packet() || port.bytesAvailable() != 0 || !port.atDelimiter ||
		    !discovery.accept(input(bytes))) {
			fail(QStringLiteral("Invalid serial Descriptor discovery reply"));
			return;
		}
		awaiting = false;
		timeout.stop();
		if (discovery.complete()) {
			admitted = true;
			discoveryTimeout.stop();
			auto* target = &owner;
			const auto descriptor = discovery.descriptor();
			later(0, [this, target, descriptor] {
				if (admitted)
					Q_EMIT target->descriptorReceived(descriptor);
			});
		} else {
			later(0, [this] {
				bootstrap();
			});
		}
	}

	void service()
	{
		if (!open || handling || !adapter || !link)
			return;
		handling = true;
		link->poll(adapter->now_ms());
		const auto error = adapter->take_transport_error();
		unsigned budget = 2;
		while (open && budget-- != 0 && link->has_packet()) {
			QByteArray bytes;
			{
				auto packet = link->pop_packet();
				const auto body = packet.data();
				bytes = {reinterpret_cast<const char*>(body.data()),
				         static_cast<qsizetype>(body.size())};
			}
			received(std::move(bytes));
		}
		handling = false;
		if (open && error != adapters::qt::TransportError::None)
			fail(QStringLiteral("Serial driver reported a transport error"));
	}
};

SerialTransport::SerialTransport(QObject* parent)
    : DeviceTransport(parent), impl_(std::make_unique<Impl>(*this))
{}

SerialTransport::~SerialTransport()
{
	impl_->close();
}

void SerialTransport::connectTo(const QString& name, qint32 baud)
{
	impl_->close();
	impl_->identity.clear();
	impl_->discovery.reset();
	impl_->port.setPortName(name);
	if (name.trimmed().isEmpty() || baud <= 0 || !impl_->port.setBaudRate(baud) ||
	    !impl_->port.setDataBits(QSerialPort::Data8) ||
	    !impl_->port.setParity(QSerialPort::NoParity) ||
	    !impl_->port.setStopBits(QSerialPort::OneStop) ||
	    !impl_->port.setFlowControl(QSerialPort::NoFlowControl) ||
	    !impl_->port.open(QIODevice::ReadWrite)) {
		auto* target = this;
		const auto reason =
		    QStringLiteral("Cannot open serial port: %1").arg(impl_->port.errorString());
		impl_->later(0, [target, reason] {
			Q_EMIT target->transportError(reason);
		});
		return;
	}
	const QSerialPortInfo observed{impl_->port};
	auto location = observed.systemLocation();
	if (location.isEmpty())
		location = QFileInfo(name).canonicalFilePath();
	if (location.isEmpty())
		location = name;
	const QJsonObject identity{
	    {QStringLiteral("systemLocation"), location},
	    {QStringLiteral("serialNumber"), observed.serialNumber()},
	    {QStringLiteral("vendorId"),
	     observed.hasVendorIdentifier() ? int(observed.vendorIdentifier()) : -1},
	    {QStringLiteral("productId"),
	     observed.hasProductIdentifier() ? int(observed.productIdentifier()) : -1}};
	impl_->identity = QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
	(void)impl_->port.clear(QSerialPort::AllDirections);
	impl_->port.atDelimiter = true;
	impl_->link = std::make_unique<Link>();
	impl_->adapter = std::make_unique<Adapter>(impl_->port, *impl_->link);
	impl_->open = true;
	if (!impl_->adapter->bind()) {
		impl_->fail(QStringLiteral("Cannot bind the serial COBS adapter"));
		return;
	}
	impl_->adapter->set_service_handler([state = impl_.get()] {
		state->scheduleService();
	});
	// A delimiter terminates any old partial request at the peer. It neither
	// replays a request nor proves remote session cleanup.
	if (impl_->port.write("\0", 1) != 1) {
		impl_->fail(QStringLiteral("Cannot synchronize the serial COBS stream"));
		return;
	}
	impl_->serviceTimer.start();
	impl_->discoveryTimeout.start(180000);
	impl_->later(0, [state = impl_.get()] {
		state->bootstrap();
	});
}

void SerialTransport::close()
{
	impl_->close();
}

bool SerialTransport::ready() const noexcept
{
	return impl_->open && impl_->admitted;
}

bool SerialTransport::canStage() const noexcept
{
	return ready() && !impl_->awaiting && !impl_->stagedMessage && !impl_->link->tx_active() &&
	       impl_->port.bytesToWrite() == 0;
}

bool SerialTransport::stage(telemetry::client::Packet packet)
{
	if (!canStage() || !packet || packet.bytes.size() > inspector_serial::maxBodyBytes)
		return false;
	const auto parsed = encoded::parse(packet.bytes);
	if (!parsed || parsed.header.reply ||
	    parsed.header.expectedResponseBytes > inspector_serial::maxBodyBytes - encoded::headerBytes)
		return false;
	auto message = impl_->link->make_message(packet.bytes.size());
	if (!message ||
	    !message.append_bytes(std::span<const std::uint8_t>{
	        reinterpret_cast<const std::uint8_t*>(packet.bytes.data()), packet.bytes.size()}))
		return false;
	impl_->stagedMessage.emplace(std::move(message));
	impl_->session = packet.session;
	impl_->requestId = packet.requestId;
	return true;
}

bool SerialTransport::sendStaged()
{
	if (!ready() || !impl_->stagedMessage)
		return false;
	impl_->awaiting = true;
	impl_->timeout.start(impl_->timeoutMs);
	const auto sent = impl_->sendMessage(*impl_->stagedMessage);
	impl_->stagedMessage.reset();
	if (!sent) {
		impl_->fail(QStringLiteral("Serial frame transmission refused or incomplete"));
		return false;
	}
	auto* target = this;
	const auto session = impl_->session, id = impl_->requestId;
	impl_->later(0, [target, session, id] {
		Q_EMIT target->packetAccepted(session, id);
	});
	return true;
}

void SerialTransport::discardStaged()
{
	impl_->stagedMessage.reset();
}

bool SerialTransport::locallyQuiescent() const noexcept
{
	return !impl_->open && !impl_->port.isOpen() && !impl_->link && !impl_->adapter &&
	       !impl_->stagedMessage;
}

QString SerialTransport::peerAddress() const
{
	return impl_->identity;
}

void SerialTransport::setWriteChunkBytes(int bytes) noexcept
{
	impl_->port.writeLimit = bytes > 0 ? bytes : std::numeric_limits<int>::max();
}

void SerialTransport::setTimeout(int milliseconds) noexcept
{
	impl_->timeoutMs = std::clamp(milliseconds, 1, 60000);
}
} // namespace inspector_qml
