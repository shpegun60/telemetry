/* Actual serial CLI and board Protocol over real host PTYs. MIT. */
#include "../../examples/nucleo_h7s3l8/Protocol.hpp"
#include "../../examples/inspector_serial/SerialProfile.hpp"
#include <cobs/Cobs.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QProcess>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <array>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_UNIX
#include <cerrno>
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
			std::fprintf(stderr, "ReadMany serial line %d: %s\n", __LINE__, #x);                   \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)
#ifdef Q_OS_UNIX
using Link = cobs::Endpoint<wire::Pool<4, 1>, inspector_serial::FrameFormat>;
namespace ew = telemetry::protocol::encoded;

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
	unsigned batches = 0, mutations = 0, lists = 0;

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
			else if (encoded.header.operation != ew::Operation::FieldRead)
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
		if (size == 0)
			return;
		auto message = link_.make_message(size);
		CHECK(message &&
		      message.append_bytes({reinterpret_cast<const std::uint8_t*>(reply_.data()), size}));
		CHECK(link_.send(message) == wire::SendResult::Sent);
	}
};
#endif
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app{argc, argv};
#ifdef Q_OS_UNIX
	CHECK(argc == 3);
	const QString cli = QString::fromLocal8Bit(argv[1]);
	const QString output = QString::fromLocal8Bit(argv[2]);
	for (int fault = 0; fault < 3; ++fault) {
		Peer peer{fault};
		QProcess child;
		const auto path = output + QStringLiteral("/serial-%1.json").arg(fault);
		child.start(cli, {"--serial", peer.name(), "--baud", "115200", "--report", path});
		CHECK(child.waitForStarted(5000));
		QElapsedTimer deadline;
		deadline.start();
		while (child.state() != QProcess::NotRunning && deadline.elapsed() < 60000) {
			QCoreApplication::processEvents();
			QThread::msleep(1);
		}
		if (child.state() != QProcess::NotRunning) {
			child.kill();
			child.waitForFinished(5000);
			CHECK(false);
		}
		const auto log = child.readAllStandardOutput() + child.readAllStandardError();
		std::printf("ReadMany serial fault=%d exit=%d batches=%u lists=%u\n%s", fault,
		            child.exitCode(), peer.batches, peer.lists, log.constData());
		QFile file{path};
		CHECK(file.open(QIODevice::ReadOnly));
		const auto report = QJsonDocument::fromJson(file.readAll()).object();
		CHECK(report["passed"].toBool() == (fault == 0));
		CHECK((child.exitCode() == 0) == (fault == 0));
		CHECK(peer.mutations == 0);
		if (fault == 0) {
			CHECK(report["cases"].toArray().size() == 4);
			CHECK(peer.batches == 7 && peer.lists == 2);
			CHECK(report["lifecycle"].toObject()["noReplay"].toBool());
		} else if (fault == 1)
			CHECK(peer.batches == 0);
		else
			CHECK(peer.batches == 1);
	}
	std::printf("ReadMany Serial checks=%u pty=1\n", checks);
#else
	(void)argv;
	CHECK(argc >= 1);
	std::printf("ReadMany Serial checks=%u pty=0\n", checks);
#endif
	return 0;
}
