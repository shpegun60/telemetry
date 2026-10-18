/*
 * @file main.cpp
 * @brief One generated QML UI over real native Encoded or Resource/RPC dispatch.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "../../tests/codegen/Device.hpp"
#include <DeviceApi.hpp>
#include <QmlRpcAdapter.hpp>
#include <resource/FileSystem.hpp>
#include <resource/protocol/Protocol.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <span>

namespace cf = codegen_fixture;
namespace qa = device_api::qml;
namespace rpc = resource::telemetry::v3;

static QByteArray load(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

static void direct(qa::DeviceApi& api, const QString& token, quint32 session, int operation,
                   quint32 id, const QByteArray& payload)
{
	if (!api.requestActive(token, session))
		return;
	constexpr auto capacity =
	    std::max(cf::model.maxFieldWireSize(), cf::model.maxServiceResponseWireSize());
	std::array<std::byte, capacity> output{};
	const auto input =
	    std::span<const std::byte>{reinterpret_cast<const std::byte*>(payload.constData()),
	                               static_cast<std::size_t>(payload.size())};
	telemetry::DispatchStatus dispatch = telemetry::DispatchStatus::InternalError;
	int endpoint = 0;
	std::uint32_t written = 0;
	switch (operation) {
		case 4: {
			const auto result =
			    telemetry::readFieldEncoded(cf::model.view(), id, output, cf::workspace);
			dispatch = result.dispatch;
			written = result.written;
			break;
		}
		case 1: {
			const auto result =
			    telemetry::writeFieldEncoded(cf::model.view(), id, input, cf::workspace);
			dispatch = result.dispatch;
			endpoint = static_cast<int>(result.endpointStatus);
			break;
		}
		case 2: {
			const auto result =
			    telemetry::executeCommandEncoded(cf::model.view(), id, input, cf::workspace);
			dispatch = result.dispatch;
			endpoint = static_cast<int>(result.endpointStatus);
			break;
		}
		case 3: {
			const auto result =
			    telemetry::callServiceEncoded(cf::model.view(), id, input, output, cf::workspace);
			dispatch = result.dispatch;
			endpoint = static_cast<int>(result.endpointStatus);
			written = result.written;
			break;
		}
		default:
			api.fail(token, session, QStringLiteral("Protocol"),
			         QStringLiteral("Unknown operation"));
			return;
	}
	if (written > output.size()) {
		api.fail(token, session, QStringLiteral("Protocol"),
		         QStringLiteral("Invalid native written count"));
		return;
	}
	api.complete(token, session, static_cast<int>(dispatch), endpoint,
	             QByteArray(reinterpret_cast<const char*>(output.data()), written));
}

// The loopback exchanges real fragmented Resource frames. It uses no socket,
// device or nested event loop. The timer and borrowed-client adapter die before
// the client/buffers below; all native fixture callbacks are small synchronous work.
struct RpcLoopback {
	std::array<std::byte, cf::model.maxScratch()> scratch{};
	telemetry::Workspace workspace{scratch};
	std::array<std::byte, 128> request{}, response{}, clientRequest{}, clientResponse{};
	std::array<std::byte, 48> packet{};
	std::array<std::byte, 64> reply{};
	telemetry::ModelView model{cf::model.view()};
	rpc::RpcMailbox mailbox{model, workspace, request, response};
	resource::FileSystem<1> files{std::array{resource::file("/rpc", mailbox)}};
	rpc::RpcClient client{0, clientRequest, clientResponse, packet};
	qa::RpcAdapter<> adapter;
	QTimer timer;
	QElapsedTimer clock;

	explicit RpcLoopback(qa::DeviceApi& api, QCoreApplication& app) : adapter(api, client)
	{
		QObject::connect(
		    &api, &qa::DeviceApi::sessionChanged, &adapter,
		    [this] {
			    mailbox.resetSession();
		    },
		    Qt::QueuedConnection);
		QObject::connect(&timer, &QTimer::timeout, &adapter, [this, &app] {
			if (!pump())
				app.exit(1);
		});
		timer.setInterval(1);
		clock.start();
		timer.start();
	}

	bool pump()
	{
		const auto now = static_cast<quint32>(clock.elapsed());
		adapter.process(now);
		if (const auto pending = adapter.nextPacket()) {
			if (!adapter.onSendAccepted(pending.generation, pending.requestId))
				return false;
			const auto result = resource::protocol::process(files.view(), pending.bytes, reply);
			if (result.status != resource::Status::Ok)
				return false;
			if (!adapter.onReply(std::span(reply).first(result.written), pending.generation,
			                     pending.requestId))
				return false;
		}
		mailbox.process(now);
		return true;
	}
};

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 3) {
		std::cerr << "Usage: telemetry_qml_example descriptor.bin config.bin [--rpc] [--smoke]\n";
		return 2;
	}
	bool useRpc = false, smoke = false;
	for (int index = 3; index < argc; ++index) {
		const QByteArray option{argv[index]};
		if (option == "--rpc")
			useRpc = true;
		else if (option == "--smoke")
			smoke = true;
		else
			return 2;
	}
	const auto descriptor = load(QString::fromLocal8Bit(argv[1]));
	const auto config = load(QString::fromLocal8Bit(argv[2]));
	const QByteArray actualDescriptor{reinterpret_cast<const char*>(cf::descriptorBytes.data()),
	                                  static_cast<qsizetype>(cf::descriptorBytes.size())};
	if (descriptor != actualDescriptor ||
	    telemetry::decode(
	        std::span<const std::byte>{reinterpret_cast<const std::byte*>(config.constData()),
	                                   static_cast<std::size_t>(config.size())},
	        client_example::device.config) != telemetry::CodecStatus::Ok)
		return 2;
	QQmlApplicationEngine engine;
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/Example/Main.qml")));
	if (engine.rootObjects().isEmpty())
		return 1;
	auto* root = engine.rootObjects().first();
	root->setProperty("transportMode", useRpc ? "Resource/RPC loopback" : "Native Encoded");
	auto* api = root->findChild<qa::DeviceApi*>("deviceApi");
	if (!api)
		return 1;
	api->setInputEngine(&engine);
	std::unique_ptr<RpcLoopback> loopback;
	if (useRpc)
		loopback = std::make_unique<RpcLoopback>(*api, app);
	else
		QObject::connect(
		    api, &qa::DeviceApi::requestReady, api,
		    [guard = QPointer<qa::DeviceApi>(api)](QString token, quint32 session, int operation,
		                                           quint32 id, quint32, quint32,
		                                           QByteArray payload) {
			    if (guard)
				    direct(*guard, token, session, operation, id, payload);
		    },
		    Qt::QueuedConnection);
	if (!api->admitDescriptor(descriptor.left(64)))
		return 1;
	if (smoke) {
		QObject::connect(api, &qa::DeviceApi::operationFinished, &app,
		                 [&app](const QString&, const QVariantMap& result) {
			                 app.exit(result.value("success").toBool() ? 0 : 1);
		                 });
		QTimer::singleShot(5000, &app, [&app] {
			app.exit(3);
		});
	}
	api->device()->refreshConfig();
	return app.exec();
}
