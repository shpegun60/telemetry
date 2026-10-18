/*
 * Actual QML, native Encoded and Resource/RPC qualification of the generated API.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
 */
#include "../codegen/Device.hpp"
#include <DeviceApi.hpp>
#include <QmlRpcAdapter.hpp>
#include <resource/protocol/Protocol.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace cf = codegen_fixture;
namespace ts = telemetry;
namespace rpc = resource::telemetry::v3;
namespace qa = device_api::qml;
static unsigned checks = 0;

static void check(bool ok, const char* message)
{
	++checks;
	if (!ok)
		throw std::runtime_error(message);
}

static void spin(const std::function<bool()>& done, const std::function<void()>& pump = {})
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 7000) {
		QCoreApplication::processEvents();
		if (pump)
			pump();
		QThread::msleep(1);
	}
	check(done(), "bounded asynchronous operation timed out");
}

static QByteArray fixture(const QString& directory, const char* name)
{
	QFile file(directory + QLatin1Char('/') + QString::fromLatin1(name));
	check(file.open(QIODevice::ReadOnly), "cannot open native fixture");
	return file.readAll();
}

static QString invoke(QObject* object, const char* method)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token)), method);
	check(!token.isEmpty(), "operation token empty");
	return token;
}

static QString invoke(QObject* object, const char* method, const QVariant& value)
{
	QString token;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QString, token),
	                                Q_ARG(QVariant, value)),
	      method);
	check(!token.isEmpty(), "operation token empty");
	return token;
}

static QString qmlInvoke(QObject* object, const char* method)
{
	QVariant value;
	check(QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, value)), method);
	check(!value.toString().isEmpty(), "QML operation token empty");
	return value.toString();
}

static QVariantMap await(QSignalSpy& spy, const QString& token,
                         const std::function<void()>& pump = {})
{
	const auto locate = [&]() {
		for (const auto& args : spy)
			if (args[0].toString() == token)
				return true;
		return false;
	};
	spin(locate, pump);
	for (qsizetype i = 0; i < spy.size(); ++i) {
		if (spy[i][0].toString() == token) {
			const auto result = spy.takeAt(i)[1].toMap();
			check(result.contains("success") && result.contains("dispatchStatus") &&
			          result.contains("endpointStatus") && result.contains("accepted"),
			      "result evidence keys");
			return result;
		}
	}
	throw std::runtime_error("missing result");
}

static void success(const QVariantMap& result)
{
	if (!result.value("success").toBool())
		std::cerr << "unexpected refusal " << result.value("errorKind").toString().toStdString()
		          << ": " << result.value("message").toString().toStdString() << '\n';
	check(result.value("success").toBool(), "expected success");
	check(result.value("dispatchStatus").toInt() == 0, "successful dispatch evidence");
}

static void refused(const QVariantMap& result, const char* kind)
{
	check(!result.value("success").toBool() && !result.value("accepted").toBool(),
	      "refused success/accepted");
	if (result.value("errorKind").toString() != QLatin1String(kind))
		std::cerr << "expected " << kind << " got "
		          << result.value("errorKind").toString().toStdString() << '\n';
	check(result.value("errorKind").toString() == QLatin1String(kind), "refusal kind");
}

struct Request {
	QString token;
	quint32 session;
	int op;
	quint32 id, requestType, responseType;
	QByteArray bytes;
};

static void direct(qa::DeviceApi& api, const Request& r)
{
	std::array<std::byte, 4096> output{};
	const auto input =
	    std::as_bytes(std::span{r.bytes.constData(), static_cast<std::size_t>(r.bytes.size())});
	ts::DispatchStatus dispatch = ts::DispatchStatus::InternalError;
	int endpoint = 0;
	std::uint32_t written = 0;
	switch (r.op) {
		case 4: {
			const auto v = ts::readFieldEncoded(cf::model.view(), r.id, output, cf::workspace);
			dispatch = v.dispatch;
			written = v.written;
			break;
		}
		case 1: {
			const auto v = ts::writeFieldEncoded(cf::model.view(), r.id, input, cf::workspace);
			dispatch = v.dispatch;
			endpoint = static_cast<int>(v.endpointStatus);
			break;
		}
		case 2: {
			const auto v = ts::executeCommandEncoded(cf::model.view(), r.id, input, cf::workspace);
			dispatch = v.dispatch;
			endpoint = static_cast<int>(v.endpointStatus);
			break;
		}
		case 3: {
			const auto v =
			    ts::callServiceEncoded(cf::model.view(), r.id, input, output, cf::workspace);
			dispatch = v.dispatch;
			endpoint = static_cast<int>(v.endpointStatus);
			written = v.written;
			break;
		}
		default:
			throw std::runtime_error("unexpected explicit operation");
	}
	api.complete(r.token, r.session, static_cast<int>(dispatch), endpoint,
	             QByteArray(reinterpret_cast<const char*>(output.data()), written));
}

static void rpcLifetimes(const QByteArray& header)
{
	// Independent sessions keep cancellation/destruction probes separate from
	// ordinary successful request execution. No native model packet is applied.
	for (int scenario = 0; scenario < 5; ++scenario) {
		std::array<std::byte, 4096> request{}, response{};
		std::array<std::byte, 64> packet{};
		rpc::RpcClient client{0, request, response, packet};
		auto api = std::make_unique<qa::DeviceApi>();
		check(api->admitDescriptor(header), "RPC lifecycle admission");
		QSignalSpy finished(api.get(), &qa::DeviceApi::operationFinished);
		auto adapter = std::make_unique<qa::RpcAdapter<>>(*api, client);
		check(adapter->beginSession() == rpc::RpcClientStatus::Ok, "RPC lifecycle session");
		api->setRequestTimeoutMs(scenario < 2 ? 20 : 5000);
		const auto token =
		    scenario == 0 ? invoke(api->device(), "refreshConfig") : invoke(api->device(), "reset");
		QCoreApplication::sendPostedEvents(api.get(), QEvent::MetaCall);
		if (scenario == 0) {
			// requestReady is queued, but the receiving adapter has not run.
			QThread::msleep(30);
			QCoreApplication::sendPostedEvents(adapter.get(), QEvent::MetaCall);
			// Force adapter-posted failures ahead of timer events on every Qt
			// version. The expired token must retain the API's timeout result.
			QCoreApplication::sendPostedEvents(api.get(), QEvent::MetaCall);
			check(!adapter->nextPacket(), "expired queued request began RPC");
			refused(await(finished, token), "Timeout");
			continue;
		}
		QCoreApplication::sendPostedEvents(adapter.get(), QEvent::MetaCall);
		check(client.active(), "RPC lifecycle request did not become active");
		if (scenario == 1) {
			QThread::msleep(30);
			check(!adapter->nextPacket(), "expired unsent RPC produced a packet");
			check(client.diagnostic().status == rpc::RpcClientStatus::Cancelled,
			      "unsent RPC did not retain exact native cancellation evidence");
			QCoreApplication::sendPostedEvents(api.get(), QEvent::MetaCall);
			refused(await(finished, token), "OutcomeUnknown");
		} else if (scenario == 2) {
			const auto outgoing = adapter->nextPacket();
			check(bool(outgoing), "RPC final handoff packet missing");
			check(adapter->onSendAccepted(outgoing.generation, outgoing.requestId),
			      "RPC final handoff acceptance");
			adapter->loseSession(outgoing.generation);
			refused(await(finished, token), "OutcomeUnknown");
			spin([&] {
				return !api->ready();
			});
		} else if (scenario == 3) {
			api.reset();
			check(!adapter->nextPacket(), "destroyed API left a sendable RPC packet");
			check(!client.active(), "destroyed API left RPC active");
		} else {
			adapter.reset();
			refused(await(finished, token), "Transport");
			spin([&] {
				return !api->ready();
			});
		}
	}
}

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	try {
		std::cout << "Qt runtime=" << qVersion() << " compiler=" << __VERSION__ << '\n';
		check(argc == 2, "expected native fixture directory");
		const auto directory = QString::fromLocal8Bit(argv[1]);
		const auto descriptor = fixture(directory, "descriptor.bin");
		const auto configBytes = fixture(directory, "config.bin");
		const auto requestBytes = fixture(directory, "request.bin");
		QQmlEngine engine;
		QQmlComponent component(&engine);
		component.loadUrl(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/Tests/Smoke.qml")));
		if (component.isError())
			std::cerr << component.errorString().toStdString();
		std::unique_ptr<QObject> root(component.create());
		check(root != nullptr, "actual generated QML module failed to instantiate");
		auto* api = qobject_cast<qa::DeviceApi*>(root->property("api").value<QObject*>());
		check(api != nullptr, "QML instantiated wrong API type");
		api->setInputEngine(&engine);
		auto* device = api->device();
		auto* edge = api->edge();
		QSignalSpy finished(api, &qa::DeviceApi::operationFinished);
		QSignalSpy cacheChanges(device, &qa::Group_0Api::configChanged);
		check(finished.isValid() && cacheChanges.isValid(), "signal metadata");
		QList<Request> requests;
		bool automatic = true;
		auto connection = QObject::connect(
		    api, &qa::DeviceApi::requestReady, api,
		    [&](QString token, quint32 session, int op, quint32 id, quint32 q, quint32 p,
		        QByteArray bytes) {
			    requests.append({token, session, op, id, q, p, bytes});
			    if (automatic && api->requestActive(token, session))
				    direct(*api, requests.last());
		    },
		    Qt::QueuedConnection);
		refused(await(finished, qmlInvoke(root.get(), "refresh")), "Schema");
		check(requests.isEmpty(), "unadmitted API dispatched");
		check(!api->admitDescriptor(descriptor.left(63)), "short header admitted");
		for (int i = 0; i < 64; ++i) {
			auto bad = descriptor.left(64);
			bad[i] = char(static_cast<unsigned char>(bad[i]) ^ 1);
			check(!api->admitDescriptor(bad), "corrupted declared header admitted");
		}
		check(api->admitDescriptor(descriptor.left(64)) && api->ready(), "valid admission");
		success(await(finished, qmlInvoke(root.get(), "refresh")));
		check(device->configValid() && root->property("cacheValid").toBool(),
		      "cache validity binding");
		const auto config = device->config().toMap();
		check(config.value("serial").typeId() == QMetaType::QString &&
		          config.value("serial").toString() == "18446744073709551615",
		      "u64 QString exactness");
		check(config.value("offset").toString() == "-9223372036854775808", "i64 QString exactness");
		check(config.value("gain").toFloat() == 0 && std::signbit(config.value("gain").toFloat()),
		      "F32 negative zero");
		QVariant answer;
		check(QMetaObject::invokeMethod(root.get(), "wideExact", Q_RETURN_ARG(QVariant, answer)) &&
		          answer.toBool(),
		      "QML u64 exactness");
		check(
		    QMetaObject::invokeMethod(root.get(), "negativeZero", Q_RETURN_ARG(QVariant, answer)) &&
		        answer.toBool(),
		    "QML negative zero");
		const auto count = requests.size();
		for (int i = 0; i < 1000; ++i) {
			(void)device->config();
			(void)device->configValid();
		}
		QQuickWindow window;
		window.resize(64, 64);
		QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
		qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
		window.show();
		spin([&] {
			return !frames.isEmpty();
		});
		window.hide();
		check(requests.size() == count && cacheChanges.size() == 1,
		      "property getter performed I/O or spurious NOTIFY");
		success(await(finished, qmlInvoke(root.get(), "write")));
		check(requests.last().op == 1 && requests.last().id == 0 &&
		          requests.last().bytes == configBytes,
		      "QML object input differs from native wire bytes");
		check(!device->configValid(), "successful write must invalidate cache");
		success(await(finished, invoke(device, "apply", config)));
		check(requests.last().op == 2 && requests.last().bytes == configBytes,
		      "command native bytes");
		success(await(finished, invoke(device, "reset")));
		QVariantMap request{{"config", config}, {"multiplier", 4}};
		success(await(finished, invoke(device, "inspect", request)));
		check(requests.last().op == 3 && requests.last().bytes == requestBytes,
		      "service native bytes");
		for (int status = 0; status <= 3; ++status) {
			request["multiplier"] = status;
			const auto r = await(finished, invoke(device, "inspect", request));
			refused(r, "Endpoint");
			check(r.value("endpointStatus").toInt() == (status == 0   ? 1
			                                            : status == 1 ? 3
			                                            : status == 2 ? 2
			                                                          : 4),
			      "raw endpoint evidence");
		}
		request["multiplier"] = 4;
		const auto offline = await(finished, invoke(device, "offline", request));
		refused(offline, "Dispatch");
		check(offline.value("dispatchStatus").toInt() == 9, "late binding evidence");
		success(await(finished, invoke(device, "ping")));
		success(await(finished, invoke(edge, "good")));
		refused(await(finished, invoke(edge, "fail_2")), "Endpoint");
		success(await(finished, invoke(edge, "consume", QVariantMap{})));
		const auto accepted = await(finished, invoke(edge, "accept", QVariantMap{}));
		check(!accepted.value("success").toBool() && accepted.value("accepted").toBool() &&
		          accepted.value("errorKind").toString().isEmpty(),
		      "Accepted work evidence lost");
		success(await(finished, invoke(edge, "refreshScalars")));
		const auto scalars = edge->scalars().toMap();
		check(scalars.value("u64").toString() == "18446744073709551615" &&
		          scalars.value("i64").toString() == "-9223372036854775808",
		      "all scalar 64bit strings");
		check(std::isinf(scalars.value("f64").toDouble()), "IEEE infinity lost");
		auto before = requests.size();
		for (const auto method :
		     {"badWide", "throwingGetter", "typedArray", "sparseArray", "cycle"})
			refused(await(finished, qmlInvoke(root.get(), method)), "Validation");
		for (const auto& invalid : {QVariant{42.0}, QVariant{"18446744073709551616"},
		                            QVariant{"01"}, QVariant{"+1"}, QVariant{" 1"}}) {
			auto bad = config;
			bad["serial"] = invalid;
			refused(await(finished, invoke(device, "writeConfig", bad)), "Validation");
		}
		for (const auto& invalid : {QVariant{"true"}, QVariant{1}}) {
			auto bad = config;
			bad["enabled"] = invalid;
			refused(await(finished, invoke(device, "writeConfig", bad)), "Validation");
		}
		auto bad = config;
		bad["gain"] = 1e100;
		refused(await(finished, invoke(device, "writeConfig", bad)), "Validation");
		check(requests.size() == before, "validation refusal dispatched");
		success(await(finished, qmlInvoke(root.get(), "getterOnce")));
		check(requests.last().bytes == configBytes, "QML getter read repeatedly during encoding");
		before = requests.size();
		refused(await(finished, qmlInvoke(root.get(), "getterReset")), "Validation");
		check(requests.size() == before && !api->ready(),
		      "getter session change dispatched to another peer");
		check(api->admitDescriptor(descriptor.left(64)), "readmission after getter session change");
		auto ieee = config;
		ieee["gain"] = std::numeric_limits<double>::quiet_NaN();
		success(await(finished, invoke(device, "writeConfig", ieee)));
		success(await(finished, invoke(device, "refreshConfig")));
		check(std::isnan(device->config().toMap().value("gain").toDouble()), "NaN lost");
		success(await(finished, invoke(device, "writeConfig", config)));

		automatic = false;
		const auto pending = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == pending;
		});
		refused(await(finished, invoke(device, "refreshConfig")), "Busy");
		api->complete(pending, api->session(), 0, 0, QByteArray(1, '\0'));
		refused(await(finished, pending), "Codec");
		const auto boolToken = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == boolToken;
		});
		auto malformedBool = configBytes;
		malformedBool[28] = 2;
		api->complete(boolToken, api->session(), 0, 0, malformedBool);
		refused(await(finished, boolToken), "Codec");
		const auto stale = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == stale;
		});
		const auto oldSession = api->session();
		api->resetSession();
		refused(await(finished, stale), "Cancelled");
		check(!device->configValid() && !api->ready(), "reset cache/session invalidation");
		check(api->admitDescriptor(descriptor.left(64)) && api->session() != oldSession,
		      "session renewal");
		const auto fresh = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == fresh;
		});
		api->complete(fresh, oldSession, 0, 0, configBytes);
		QCoreApplication::processEvents();
		check(!device->configValid(), "stale session completed new request");
		api->complete(fresh, api->session(), 0, 0, configBytes);
		success(await(finished, fresh));
		api->complete(fresh, api->session(), 0, 0, configBytes);
		QCoreApplication::processEvents();
		check(finished.isEmpty(), "duplicate completion emitted");
		const auto threaded = invoke(device, "refreshConfig");
		spin([&] {
			return requests.last().token == threaded;
		});
		const auto threadEpoch = api->session();
		const auto notificationsBeforeThread = cacheChanges.size();
		std::thread worker([api, threaded, threadEpoch, configBytes] {
			api->complete(threaded, threadEpoch, 0, 0, configBytes);
		});
		worker.join();
		success(await(finished, threaded));
		check(cacheChanges.size() == notificationsBeforeThread + 1,
		      "worker completion failed to queue cache NOTIFY to owner");
		api->setRequestTimeoutMs(20);
		refused(await(finished, invoke(device, "refreshConfig")), "Timeout");
		refused(await(finished, invoke(device, "reset")), "OutcomeUnknown");
		// Continuous submissions must not restart the deadline timer of old work.
		const auto starvation = invoke(device, "refreshConfig");
		auto quickConnection = QObject::connect(
		    api, &qa::DeviceApi::requestReady, api,
		    [api](const QString& token, quint32 epoch, int op, quint32, quint32, quint32,
		          const QByteArray&) {
			    if (op == 2 && api->requestActive(token, epoch))
				    api->complete(token, epoch, 0, 0, {});
		    },
		    Qt::QueuedConnection);
		QTimer submissions;
		submissions.setInterval(1);
		QObject::connect(&submissions, &QTimer::timeout, api, [device] {
			device->reset();
		});
		submissions.start();
		QElapsedTimer elapsed;
		elapsed.start();
		refused(await(finished, starvation), "Timeout");
		check(elapsed.elapsed() < 500, "new requests starved old deadline");
		submissions.stop();
		QObject::disconnect(quickConnection);
		QCoreApplication::processEvents();
		finished.clear();
		api->setRequestTimeoutMs(5000);
		QObject::disconnect(connection);

		QPointer<qa::DeviceApi> victim = new qa::DeviceApi;
		check(victim->admitDescriptor(descriptor.left(64)), "deletion probe admission");
		QObject::connect(
		    victim, &qa::DeviceApi::requestReady, victim,
		    [victim, configBytes](QString token, quint32 epoch, int, quint32, quint32, quint32,
		                          QByteArray) {
			    if (victim)
				    victim->complete(token, epoch, 0, 0, configBytes);
		    },
		    Qt::QueuedConnection);
		QObject::connect(victim->device(), &qa::Group_0Api::configChanged, victim, [victim] {
			delete victim.data();
		});
		victim->device()->refreshConfig();
		spin([&] {
			return victim.isNull();
		});

		std::array<std::byte, 4096> req{}, res{}, clientReq{}, clientRes{};
		std::array<std::byte, 64> packetStorage{}, reply{};
		constexpr auto view = cf::model.view();
		rpc::RpcMailbox mailbox{view, cf::workspace, req, res};
		resource::FileSystem files{std::array{resource::file("/rpc", mailbox)}};
		rpc::RpcClient client{0, clientReq, clientRes, packetStorage};
		qa::RpcAdapter adapter{*api, client};
		check(adapter.beginSession() == rpc::RpcClientStatus::Ok, "RPC session begin");
		quint32 now = 0;
		unsigned packets = 0;
		const auto pump = [&] {
			adapter.process(now++);
			if (const auto packet = adapter.nextPacket()) {
				++packets;
				check(adapter.onSendAccepted(packet.generation, packet.requestId),
				      "RPC send acceptance");
				const auto result = resource::protocol::process(files.view(), packet.bytes, reply);
				mailbox.process(now);
				check(!adapter.onReply(std::span{reply}.first(result.written),
				                       packet.generation + 1, packet.requestId),
				      "wrong-generation RPC reply accepted");
				check(adapter.onReply(std::span{reply}.first(result.written), packet.generation,
				                      packet.requestId),
				      "real Resource RPC reply refused");
			}
		};
		const auto rpcResult = [&](const QString& token) {
			return await(finished, token, pump);
		};
		success(rpcResult(invoke(device, "refreshConfig")));
		success(rpcResult(qmlInvoke(root.get(), "write")));
		success(rpcResult(invoke(device, "apply", config)));
		success(rpcResult(invoke(device, "inspect", request)));
		for (int status = 0; status < 4; ++status) {
			request["multiplier"] = status;
			const auto r = rpcResult(invoke(device, "inspect", request));
			refused(r, "Endpoint");
			check(r.value("endpointStatus").toInt() == (status == 0   ? 1
			                                            : status == 1 ? 3
			                                            : status == 2 ? 2
			                                                          : 4),
			      "RPC endpoint evidence");
		}
		request["multiplier"] = 4;
		success(rpcResult(invoke(device, "ping")));
		success(rpcResult(invoke(edge, "good")));
		refused(rpcResult(invoke(edge, "fail_2")), "Endpoint");
		success(rpcResult(invoke(edge, "consume", QVariantMap{})));
		const auto rpcAccepted = rpcResult(invoke(edge, "accept", QVariantMap{}));
		check(!rpcAccepted.value("success").toBool() && rpcAccepted.value("accepted").toBool() &&
		          rpcAccepted.value("errorKind").toString().isEmpty(),
		      "RPC accepted evidence");
		refused(rpcResult(invoke(device, "offline", request)), "Dispatch");
		const auto simultaneous = invoke(device, "refreshConfig");
		const auto busyTransport = invoke(edge, "refreshScalars");
		success(rpcResult(simultaneous));
		refused(rpcResult(busyTransport), "Transport");
		check(packets > 20, "RPC request/result fragmentation not exercised");
		const auto queued = invoke(device, "reset");
		api->resetSession();
		refused(await(finished, queued), "Cancelled");
		const auto callsBefore = client_example::device.calls;
		for (int i = 0; i < 20; ++i) {
			QCoreApplication::processEvents();
			pump();
		}
		check(client_example::device.calls == callsBefore,
		      "queued stale request replayed after reset");
		rpcLifetimes(descriptor.left(64));
		qobject_cast<QQuickItem*>(root.get())->setParentItem(nullptr);
		std::cout << "QML checks=" << checks << " RPC packets=" << packets << '\n';
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "QML check failed after " << checks << ": " << error.what() << '\n';
		return 1;
	}
}
