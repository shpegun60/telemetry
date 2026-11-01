/*
 * @file Watchlist.cpp
 * @brief Actual Qt Watchlist over fragmented COBS PTYs and the optional board Protocol.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "../../examples/nucleo_h7s3l8/Protocol.hpp"
#include "../../examples/inspector_serial/SerialProfile.hpp"
#include <cobs/Cobs.h>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtTest/QTest>
#include <array>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>
#endif
namespace {
unsigned checks = 0;
#define CHECK(x)                                                                                   \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(x)) {                                                                                \
			std::fprintf(stderr, "Serial Watchlist line %d: %s\n", __LINE__, #x);                  \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)
#ifdef Q_OS_UNIX
using Link = cobs::Endpoint<wire::Pool<4, 1>, inspector_serial::FrameFormat>;
namespace ew = telemetry::protocol::encoded;
namespace ui = inspector_qml;

class Peer {
	int master_ = -1;
	QString name_;
	Link link_;
	nucleo_demo::Protocol protocol_;
	QByteArray tx_;
	qsizetype offset_ = 0;
	QElapsedTimer elapsed_;
	QTimer timer_;
	std::array<std::byte, 2048> reply_{};
	int fault_;

public:
	unsigned batches = 0, mutations = 0, lists = 0, fieldReads = 0;
	bool dropBatches = false;

	explicit Peer(int fault) : fault_(fault)
	{
		int slave = -1;
		char name[128]{};
		CHECK(openpty(&master_, &slave, name, nullptr, nullptr) == 0);
		termios options{};
		CHECK(tcgetattr(slave, &options) == 0);
		cfmakeraw(&options);
		CHECK(tcsetattr(slave, TCSANOW, &options) == 0);
		CHECK(fcntl(master_, F_SETFL, O_NONBLOCK) == 0);
		name_ = QString::fromLocal8Bit(name);
		::close(slave);
		CHECK(link_.bind(
		    [this](std::span<const std::uint8_t> bytes) noexcept {
			    if (!tx_.isEmpty())
				    return false;
			    tx_ = {reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())};
			    offset_ = 0;
			    return true;
		    },
		    [this]() noexcept {
			    return !tx_.isEmpty();
		    }));
		elapsed_.start();
		QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
			step();
		});
		timer_.start(1);
	}

	~Peer()
	{
		timer_.stop();
		::close(master_);
	}

	QString name() const
	{
		return name_;
	}

	void step()
	{
		std::array<std::uint8_t, 1024> bytes{};
		for (;;) {
			const auto count = ::read(master_, bytes.data(), bytes.size());
			if (count <= 0)
				break;
			link_.consume(std::span{bytes}.first(std::size_t(count)));
		}
		link_.poll(std::uint32_t(elapsed_.elapsed()));
		if (!tx_.isEmpty()) {
			// Deliberate fragmentation crosses the kernel, QSerialPort and COBS.
			const auto count = ::write(master_, tx_.constData() + offset_,
			                           std::size_t(std::min(qsizetype(17), tx_.size() - offset_)));
			if (count > 0)
				offset_ += count;
			if (offset_ == tx_.size()) {
				tx_.clear();
				offset_ = 0;
			}
			return;
		}
		if (link_.tx_active() || !link_.has_packet())
			return;
		auto packet = link_.pop_packet();
		const auto raw = packet.data();
		const resource::Input input{reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
		const auto encoded = ew::parse(input);
		if (encoded) {
			if (encoded.header.operation == ew::Operation::ReadMany)
				++batches;
			else if (encoded.header.operation == ew::Operation::FieldRead)
				++fieldReads;
			else
				++mutations;
		} else if (input.size() == 9 && input[0] == std::byte{1})
			++lists;
		auto size = protocol_.dispatch(input, reply_, std::uint32_t(elapsed_.elapsed()));
		if (fault_ == 1 && !encoded && input.size() == 13 && input[0] == std::byte{3} &&
		    input[1] == std::byte{2} && size >= 36) {
			// Syntactically complete Resource READ carrying an invalid TRM1.
			reply_[12] ^= std::byte{1};
		}
		if (fault_ == 2 && encoded && encoded.header.operation == ew::Operation::ReadMany &&
		    size > 34)
			reply_[34] ^= std::byte{1}; // valid COBS CRC, wrong first item ID
		if (size == 0 ||
		    (encoded && encoded.header.operation == ew::Operation::ReadMany && dropBatches))
			return;
		auto message = link_.make_message(size);
		CHECK(message &&
		      message.append_bytes({reinterpret_cast<const std::uint8_t*>(reply_.data()), size}));
		CHECK(link_.send(message) == wire::SendResult::Sent);
	}
};

template<class F>
bool await(F ready, int timeout = 8000)
{
	QElapsedTimer clock;
	clock.start();
	while (!ready() && clock.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QTest::qWait(1);
	}
	return ready();
}

QModelIndex endpoint(ui::EndpointModel& model, quint32 id, const QModelIndex& parent = {})
{
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		if (model.data(index, ui::EndpointModel::IsEndpointRole).toBool() &&
		    model.data(index, ui::EndpointModel::CategoryRole).toInt() == 1 &&
		    model.data(index, ui::EndpointModel::EndpointIdRole).toUInt() == id)
			return index;
		const auto child = endpoint(model, id, index);
		if (child.isValid())
			return child;
	}
	return {};
}

void add(ui::InspectorController& controller, quint32 id)
{
	auto* model = qobject_cast<ui::EndpointModel*>(controller.endpoints());
	const auto index = endpoint(*model, id);
	CHECK(index.isValid() && controller.selectEndpoint(index) && controller.addWatch());
}

QString cell(ui::InspectorController& controller, int row, int role)
{
	return controller.watchlist()->data(controller.watchlist()->index(row, 0), role).toString();
}

void checksForPeer()
{
	Peer peer{0};
	ui::InspectorController controller;
	controller.setReadManyDiscovery(true);
	CHECK(controller.connectSerial(peer.name(), 115200));
	const bool connected = await([&] {
		return controller.connected();
	});
	if (!connected)
		std::fprintf(stderr, "Watch bootstrap: %s lists=%u batches=%u\n",
		             qPrintable(controller.statusText()), peer.lists, peer.batches);
	CHECK(connected);
	CHECK(peer.lists == 1);
	add(controller, 1u << 16);        // exact U64 Counter
	add(controller, 2u << 16);        // scalar U32 catalog
	add(controller, (3u << 16) | 1u); // U64
	add(controller, (5u << 16) | 7u); // Unavailable
	CHECK(controller.refreshWatch());
	CHECK(await([&] {
		return !controller.watchPending();
	}));
	CHECK(peer.batches == 1 && peer.fieldReads == 0 && peer.mutations == 0);
	CHECK(cell(controller, 0, ui::WatchlistModel::TextRole) == "18446744073709551615");
	CHECK(cell(controller, 1, ui::WatchlistModel::TextRole) == "270544960");
	CHECK(cell(controller, 3, ui::WatchlistModel::StatusRole) == "Unavailable");
	CHECK(cell(controller, 3, ui::WatchlistModel::TextRole).isEmpty());
	controller.clearWatch();
	// All 32 independent readable Fields cross the actual GUI transport.
	add(controller, 1u << 16);
	for (quint32 catalog = 2; catalog <= 5; ++catalog)
		for (quint32 item = 0; item < (catalog == 5 ? 7u : 8u); ++item)
			add(controller, (catalog << 16) | item);
	CHECK(controller.watchCount() == 32 && controller.refreshWatch());
	CHECK(await([&] {
		return !controller.watchPending();
	}));
	CHECK(peer.batches == 2 && controller.inspector().pending() == 0);
	const auto oldSession = controller.inspector().session();
	peer.dropBatches = true;
	controller.setReadTimeout(500);
	CHECK(controller.refreshWatch());
	CHECK(await([&] {
		return peer.batches == 3;
	})); // proves physical handoff before timeout
	CHECK(await([&] {
		return !controller.watchPending();
	}));
	CHECK(!controller.connected() && controller.inspector().recoveryRequired());
	CHECK(controller.watchStatus().contains("Timeout"));
	CHECK(cell(controller, 0, ui::WatchlistModel::TextRole).isEmpty());
	controller.disconnectDevice();
	CHECK(controller.cleanupPending() && controller.watchCount() == 0);
	peer.dropBatches = false;
	CHECK(controller.connectSerial(peer.name(), 115200));
	CHECK(await([&] {
		return controller.connected();
	}));
	CHECK(!controller.cleanupPending() && controller.inspector().session() > oldSession &&
	      peer.lists == 2);
	add(controller, 1u << 16);
	CHECK(controller.refreshWatch() && await([&] {
		      return !controller.watchPending();
	      }));
	CHECK(peer.batches == 4 && peer.fieldReads == 0 && peer.mutations == 0);
	controller.disconnectDevice();
}

void malformedChecks()
{
	Peer peer{2};
	ui::InspectorController controller;
	controller.setReadManyDiscovery(true);
	CHECK(controller.connectSerial(peer.name()));
	CHECK(await([&] {
		return controller.connected();
	}));
	add(controller, 1u << 16);
	CHECK(controller.refreshWatch() && await([&] {
		      return !controller.watchPending();
	      }));
	CHECK(peer.batches == 1 && peer.fieldReads == 0);
	CHECK(cell(controller, 0, ui::WatchlistModel::TextRole).isEmpty());
	CHECK(controller.watchStatus().contains("CodecError"));
	controller.disconnectDevice();
}
#endif
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app{argc, argv};
#ifdef Q_OS_UNIX
	checksForPeer();
	malformedChecks();
	std::printf(
	    "Inspector Serial Watchlist checks=%u pty=1 batches=4 sessions=2 malformed=1 Qt=%s\n",
	    checks, qVersion());
#else
	std::printf("Inspector Serial Watchlist checks=%u pty=0 Qt=%s\n", checks, qVersion());
#endif
}
