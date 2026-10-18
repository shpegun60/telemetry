/* One generated QML facade, one bounded FIFO and one real RPC mailbox. MIT. */
#include "../codegen/Device.hpp"
#include <DeviceClientAdapter.hpp>
#include <telemetry/client/rpc/RpcBackend.hpp>
#include <resource/telemetry/v3/RpcSessionPool.hpp>
#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>
#include <cstdio>
#include <stdexcept>

namespace cf = codegen_fixture;
namespace qa = device_api::qml;
namespace rpc = resource::telemetry::v3;
namespace tc = telemetry::client;
static unsigned checks = 0, packets = 0;

static void check(bool value, const char* message)
{
	++checks;
	if (!value)
		throw std::runtime_error(message);
}

static QString invoke(QObject* object, const char* method)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token)), method);
	check(!token.isEmpty(), "nonempty token");
	return token;
}

static QString invoke(QObject* object, const char* method, const QVariant& value)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token),
	                                Q_ARG(QVariant, value)),
	      method);
	check(!token.isEmpty(), "nonempty value operation token");
	return token;
}

static void events()
{
	for (unsigned i = 0; i != 4; ++i)
		QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
	QCoreApplication::processEvents();
}

struct Harness {
	qa::DeviceApi api;
	std::uint32_t now = 0;
	std::array<std::byte, cf::model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	telemetry::ModelView model{cf::model.view()};
	std::array<std::byte, 256> request{}, response{}, clientRequest{}, clientResponse{};
	std::array<std::byte, 48> packet{}, copy{};
	std::array<std::byte, 64> reply{};
	rpc::RpcMailbox mailbox{model, workspace, request, response};
	resource::FileSystem<1> files{resource::filesystem(resource::file("/rpc", mailbox))};
	rpc::RpcSessionPool<1> pool{std::array{rpc::RpcSessionBinding{mailbox, files.view()}}};
	rpc::RpcSessionHandle remote = pool.acquire({1, 1, 0}).session;
	rpc::RpcClient native{0, clientRequest, clientResponse, packet};
	tc::Clock clock{this, [](void* self) noexcept {
		                return static_cast<Harness*>(self)->now;
	                }};
	tc::rpc::RpcBackend backend{native, clock};
	qa::DeviceClientAdapter<qa::DeviceApi, 3, 256> adapter{api, backend, clock};
	QSignalSpy finished{&api, &qa::DeviceApi::operationFinished};

	Harness()
	{
		check(api.admitDescriptor(header()), "admit observed schema header");
		events();
	}

	~Harness()
	{
		adapter.close();
		for (unsigned i = 0; i != 8 && !adapter.drained(); ++i)
			adapter.process(now++);
	}

	static QByteArray header()
	{
		return {reinterpret_cast<const char*>(cf::descriptorBytes.data()), 64};
	}

	void pump()
	{
		events();
		adapter.process(now);
		(void)pool.process(now);
		if (const auto pending = adapter.nextPacket()) {
			check(native.active(), "one native transaction active");
			std::copy(pending.bytes.begin(), pending.bytes.end(), copy.begin());
			if (adapter.prepareHandoff(pending.session, pending.requestId)) {
				check(adapter.onSendAccepted(pending.session, pending.requestId),
				      "complete packet handoff");
				const auto response =
				    pool.handle(remote, tc::Input{copy}.first(pending.bytes.size()), reply);
				check(response.status == rpc::RpcSessionStatus::Ok, "pool ingress");
				check(response.reply.status == resource::Status::Ok, "resource reply");
				if (response.ticket.serial)
					check(pool.replyAccepted(remote, response.ticket) == rpc::RpcSessionStatus::Ok,
					      "ACK-gated server dispatch");
				check(adapter.onReply(tc::Input{reply}.first(response.reply.written),
				                      pending.session, pending.requestId),
				      "paired response");
				++packets;
			}
		}
		(void)pool.process(now++);
	}

	QVariantMap result(const QString& token)
	{
		for (const auto& row : finished)
			if (row[0].toString() == token)
				return row[1].toMap();
		return {};
	}

	void wait(unsigned count)
	{
		for (unsigned step = 0; step != 400 && finished.size() < qsizetype(count); ++step)
			pump();
		events();
		check(finished.size() == qsizetype(count), "exactly one completion per token");
	}
};

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	try {
		client_example::device.calls = 0;
		cf::edge.calls = 0;
		Harness h;
		const auto read = invoke(h.api.device(), "refreshConfig");
		const auto command = invoke(h.api.device(), "reset");
		const auto service = invoke(h.api.edge(), "good");
		const auto full = invoke(h.api.device(), "ping");
		events();
		check(h.adapter.pending() == 3, "three accepted requests in one DeviceClient queue");
		check(!h.native.active(), "enqueue does not start native I/O");
		check(h.result(full).value("errorKind").toString() == QStringLiteral("QueueFull"),
		      "fourth request explicitly QueueFull");
		check(client_example::device.calls == 0 && cf::edge.calls == 0,
		      "no hidden execution queue");
		h.wait(4);
		check(h.result(read).value("success").toBool(), "queued read success");
		check(h.result(command).value("success").toBool(), "queued command success");
		check(h.result(service).value("success").toBool(), "queued zero-byte service success");
		check(client_example::device.calls == 1 && cf::edge.calls == 1,
		      "exact native callback count");
		check(h.adapter.pending() == 0, "execution queue drained");
		check(packets > 6, "actual fragmented Resource exchange");
		check(h.finished[1][0].toString() == read && h.finished[2][0].toString() == command &&
		          h.finished[3][0].toString() == service,
		      "accepted request FIFO order");
		auto config = h.result(read).value("value").toMap();
		config[QStringLiteral("enabled")] = false;
		const auto write = invoke(h.api.device(), "writeConfig", config);
		config[QStringLiteral("enabled")] = true;
		h.wait(5);
		check(h.result(write).value("success").toBool(), "queued write success");
		check(!client_example::device.config.enabled, "queued value snapshot");
		check(!h.api.device()->configValid(), "successful write invalidates QML read cache");
		const auto old = h.api.session();
		h.api.resetSession();
		events();
		h.adapter.process(h.now++);
		check(h.adapter.recoveryRequired(), "API reset requires real remote session closure");
		check(!h.adapter.confirmSessionClosed(old + 1), "wrong recovery generation refused");
		check(h.pool.release(h.remote) == rpc::RpcSessionStatus::Ok,
		      "release actual old mailbox session");
		(void)h.pool.process(h.now);
		h.remote = h.pool.acquire({1, 2, 0}).session;
		check(h.pool.valid(h.remote), "fresh remote session");
		check(h.adapter.confirmSessionClosed(old), "confirm excluded and drained session");
		check(h.api.admitDescriptor(Harness::header()), "fresh observed schema admission");
		events();
		const auto fresh = invoke(h.api.device(), "ping");
		h.wait(6);
		check(h.result(fresh).value("success").toBool(), "fresh user invocation after recovery");
		check(client_example::device.calls == 3, "recovery never replayed old invocation");
		h.api.setRequestTimeoutMs(2);
		const auto expired = invoke(h.api.device(), "reset");
		events();
		check(h.adapter.pending() == 1, "queued request before deadline");
		QThread::msleep(10);
		h.adapter.process(h.now); // Stale simulated transport clock, fresh Qt deadline.
		QCoreApplication::sendPostedEvents(&h.api, QEvent::MetaCall); // Before timers.
		events();
		check(h.result(expired).value("errorKind").toString() == QStringLiteral("Timeout"),
		      "expired unsent queued request keeps Timeout ahead of Qt timer");
		check(client_example::device.calls == 3, "expired queued command never executed");
		std::printf("QML queued checks=%u packets=%u\n", checks, packets);
		return 0;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "QML queued check=%u error=%s\n", checks, error.what());
		return 1;
	}
}
