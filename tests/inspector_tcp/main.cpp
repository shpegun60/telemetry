/*
 * @file main.cpp
 * @brief Exercise socket discovery, runtime reads and connection boundaries through the real Qt port.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include "TcpDemoServer.hpp"
#include "TcpTransport.hpp"
#include <telemetry/descriptor/Format.hpp>
#include <wire/EncodedWire.hpp>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPersistentModelIndex>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace ui = inspector_qml;

namespace {
unsigned checks = 0, reads = 0, connections = 0;

#define CHECK(expression)                                                                          \
	do {                                                                                           \
		++checks;                                                                                  \
		if (!(expression)) {                                                                       \
			std::fprintf(stderr, "Inspector TCP check %u failed at line %d: %s\n", checks,         \
			             __LINE__, #expression);                                                   \
			std::abort();                                                                          \
		}                                                                                          \
	} while (false)

template<class Predicate>
bool await(Predicate predicate, int milliseconds = 10000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	do {
		QCoreApplication::processEvents();
		if (predicate())
			return true;
		QTest::qWait(1);
	} while (elapsed.elapsed() < milliseconds);
	return predicate();
}

QModelIndex endpoint(ui::EndpointModel& model, int category, quint32 id)
{
	for (int root = 0; root < model.rowCount(); ++root) {
		const auto group = model.index(root, 0);
		for (int catalog = 0; catalog < model.rowCount(group); ++catalog) {
			const auto parent = model.index(catalog, 0, group);
			for (int row = 0; row < model.rowCount(parent); ++row) {
				const auto index = model.index(row, 0, parent);
				const auto metadata = model.endpoint(index);
				if (metadata.value("category").toInt() == category &&
				    metadata.value("id").toUInt() == id)
					return index;
			}
		}
	}
	return {};
}

QModelIndex named(ui::ValueModel& model, const QModelIndex& parent, const QString& name)
{
	if (model.canFetchMore(parent))
		model.fetchMore(parent);
	for (int row = 0; row < model.rowCount(parent); ++row) {
		const auto index = model.index(row, 0, parent);
		if (model.data(index, ui::ValueModel::NameRole).toString() == name)
			return index;
	}
	return {};
}

struct Client {
	ui::InspectorController controller;
	ui::EndpointModel& endpoints = *qobject_cast<ui::EndpointModel*>(controller.endpoints());
	ui::ValueModel& values = *qobject_cast<ui::ValueModel*>(controller.values());
	QSignalSpy finished{&controller, &ui::InspectorController::readFinished};

	void connect(ui::TcpDemoServer& peer)
	{
		CHECK(peer.port() != 0);
		CHECK(controller.connectTcp(QStringLiteral("127.0.0.1"), peer.port()));
		if (!await([&] {
			    return controller.connected();
		    })) {
			std::fprintf(stderr, "Connection status: %s\n", qPrintable(controller.statusText()));
			CHECK(false);
		}
		CHECK(peer.hasClient() && endpoints.rowCount() == 3 && !controller.busy());
		CHECK(controller.tcpHost() == QStringLiteral("127.0.0.1") &&
		      controller.tcpPort() == peer.port());
		++connections;
	}

	void select(quint32 id)
	{
		const auto field = endpoint(endpoints, 1, id);
		CHECK(field.isValid() && controller.selectEndpoint(field));
		CHECK(controller.canRefresh() && !controller.busy());
	}

	void read(quint32 id)
	{
		select(id);
		const auto before = finished.count();
		CHECK(controller.refresh());
		CHECK(controller.busy());
		CHECK(await([&] {
			return !controller.busy();
		}));
		CHECK(finished.count() == before + 1 && finished.last().at(0).toBool());
		CHECK(values.rowCount() == 1);
		++reads;
	}

	QString scalar() const
	{
		const auto value = values.data(values.index(0, 0), ui::ValueModel::ValueRole);
		CHECK(value.metaType().id() == QMetaType::QString);
		return value.toString();
	}
};

void realSocketReads()
{
	ui::TcpDemoServer peer;
	peer.setDescriptorChunkBytes(37);
	peer.setReplyFragmentBytes(7);
	CHECK(peer.listen());
	Client client;
	client.connect(peer);
	const auto indices = peer.resourceReadIndices();
	CHECK(indices.size() > 1);
	for (const auto index : indices)
		CHECK(index == 1); // LIST must discover Descriptor after the decoy file.
	CHECK(peer.fieldReads() == 0 && !client.controller.canRefresh());
	for (const int category : {2, 3}) {
		CHECK(client.controller.selectEndpoint(endpoint(client.endpoints, category, 0)));
		CHECK(!client.controller.canRefresh() && !client.controller.refresh());
	}
	CHECK(peer.fieldReads() == 0);

	client.read(65536);
	CHECK(client.scalar() == QStringLiteral("18446744073709551615"));
	const QPersistentModelIndex old{client.values.index(0, 0)};
	client.read(65537);
	CHECK(!old.isValid());
	CHECK(client.scalar() == QStringLiteral("-9223372036854775808"));
	client.read(65538);
	CHECK(client.scalar() == QStringLiteral("-2 (unknown)"));
	client.read(131074);
	const auto nested = client.values.index(0, 0);
	const auto matrix = named(client.values, nested, QStringLiteral("Matrix"));
	const auto row = named(client.values, matrix, QStringLiteral("[1]"));
	const auto cell = named(client.values, row, QStringLiteral("[2]"));
	CHECK(cell.isValid());
	CHECK(client.values.data(cell, ui::ValueModel::ValueRole).toString() == QStringLiteral("-6"));
	CHECK(client.values
	          .data(named(client.values, nested, QStringLiteral("Enabled")),
	                ui::ValueModel::ValueRole)
	          .toString() == QStringLiteral("true"));

	peer.setReplyFragmentBytes(127);
	client.read(131072);
	const auto array = client.values.index(0, 0);
	CHECK(client.values.data(array, ui::ValueModel::ChildCountRole).toUInt() == 16384);
	CHECK(client.values.rowCount(array) == 0);
	for (int page = 0; page < 3; ++page) {
		client.values.fetchMore(array);
		CHECK(client.values.rowCount(array) == (page + 1) * 128);
		CHECK(client.values
		          .data(client.values.index(page * 128 + 127, 0, array), ui::ValueModel::ValueRole)
		          .toString() == QString::number(page * 128 + 127));
	}
	client.read(131073);
	CHECK(client.values.rowCount() == 1 && !client.values.hasChildren(client.values.index(0, 0)));
	CHECK(peer.fieldReads() == 6 && peer.receivedBytes() > 100 && peer.sentBytes() > 32768);
	client.controller.disconnectDevice();
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	CHECK(!client.controller.connected() && client.values.rowCount() == 0 &&
	      client.endpoints.rowCount() == 0);
}

void connectionBoundaries()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	Client client;
	client.controller.setTcpWriteChunkBytes(3);
	client.connect(peer);
	client.read(65536);
	peer.setPrependPreviousEncodedReply();
	client.read(65537); // Old ID plus the current reply in the same TCP byte stream.
	CHECK(client.scalar() == QStringLiteral("-9223372036854775808"));
	const auto oldEndpoint = endpoint(client.endpoints, 1, 65536);
	const auto oldValue = client.values.index(0, 0);
	const auto oldSession = client.controller.inspector().session();
	peer.setReplyDelay(200);
	client.select(65536);
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return peer.fieldReads() == 3;
	}));
	client.controller.disconnectDevice();
	CHECK(!client.controller.connected() && !client.controller.busy());
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	peer.setReplyDelay(0);
	client.connect(peer);
	CHECK(client.controller.inspector().session() != oldSession);
	CHECK(client.endpoints.endpoint(oldEndpoint).isEmpty() &&
	      !client.values.data(oldValue).isValid());
	peer.setPrependPreviousEncodedReply(); // Valid reply from the retired session, then fresh reply.
	client.read(65537);
	QTest::qWait(250);
	CHECK(client.scalar() == QStringLiteral("-9223372036854775808"));
	CHECK(client.finished.last().at(0).toBool());
	CHECK(peer.connections() == 2);

	peer.setReplyDelay(100);
	client.select(65536);
	const auto before = client.finished.count();
	CHECK(client.controller.refresh());
	CHECK(client.controller.selectEndpoint(endpoint(client.endpoints, 1, 65537)));
	CHECK(await([&] {
		return !client.controller.busy();
	}));
	CHECK(client.finished.count() == before + 1 && !client.finished.last().at(0).toBool());
	CHECK(client.values.rowCount() == 0);
	peer.setReplyDelay(0);
	client.read(65537);
	CHECK(client.scalar() == QStringLiteral("-9223372036854775808"));
	client.controller.disconnectDevice();
}

void descriptorReplacement()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	Client client;
	client.connect(peer);
	client.read(65536);
	const auto oldEndpoint = endpoint(client.endpoints, 1, 0);
	const auto oldValue = client.values.index(0, 0);
	const auto oldSession = client.controller.inspector().session();
	auto replacement = peer.descriptor();
	const auto at = replacement.indexOf("measure");
	CHECK(at >= 64);
	replacement.replace(at, 7, "monitor");
	std::uint64_t fingerprint = telemetry::descriptor::fingerprintBasis;
	for (qsizetype i = 0; i < replacement.size(); ++i) {
		fingerprint ^= i >= 16 && i < 24 ? 0 : static_cast<unsigned char>(replacement[i]);
		fingerprint *= telemetry::descriptor::fingerprintPrime;
	}
	for (unsigned i = 0; i < 8; ++i)
		replacement[16 + i] = static_cast<char>(fingerprint >> (i * 8));
	peer.setDescriptorOverride(std::move(replacement));
	CHECK(await([&] {
		return !client.controller.connected();
	}));
	CHECK(client.values.rowCount() == 0 && client.endpoints.rowCount() == 0);
	client.connect(peer);
	CHECK(client.controller.inspector().session() != oldSession);
	CHECK(client.endpoints.endpoint(oldEndpoint).isEmpty() &&
	      !client.values.data(oldValue).isValid());
	const auto changed = endpoint(client.endpoints, 1, 0);
	CHECK(client.endpoints.endpoint(changed).value("path").toString() ==
	      QStringLiteral("monitor.Voltage"));
	client.read(0);
	const auto ua = named(client.values, client.values.index(0, 0), QStringLiteral("Ua"));
	CHECK(ua.isValid());
	CHECK(client.values.data(ua, ui::ValueModel::ValueRole)
	          .toString()
	          .startsWith(QStringLiteral("230.1")));
}

void failuresAndReconnect()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	Client client;
	client.connect(peer);
	client.controller.setReadTimeout(60);
	client.select(65536);
	peer.setDropNextEncodedReply();
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return !client.controller.busy();
	}));
	CHECK(!client.finished.isEmpty() && !client.finished.last().at(0).toBool());
	CHECK(client.values.rowCount() == 0);
	client.controller.disconnectDevice();
	CHECK(client.controller.cleanupPending());
	ui::TcpDemoServer otherPeer;
	CHECK(otherPeer.listen());
	CHECK(!client.controller.connectTcp(QStringLiteral("127.0.0.1"), otherPeer.port()));
	CHECK(client.controller.cleanupPending() && !client.controller.connected());
	CHECK(otherPeer.connections() == 0);
	CHECK(client.controller.connectDemo());
	CHECK(client.controller.cleanupPending() && client.controller.connected());
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	client.controller.setReadTimeout(3000);
	client.connect(peer);
	CHECK(!client.controller.cleanupPending());
	client.read(65536);

	client.select(65536);
	peer.setCloseAfterReplyBytes(9);
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return !client.controller.connected() && !client.controller.busy();
	}));
	CHECK(client.values.rowCount() == 0 && client.endpoints.rowCount() == 0);
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	client.connect(peer);
	client.read(65537);
	client.select(65536);
	peer.setCloseAfterRequestBytes(8);
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return !client.controller.connected() && !client.controller.busy();
	}));
	CHECK(client.values.rowCount() == 0);
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	client.connect(peer);
	client.read(65536);
	CHECK(client.scalar() == QStringLiteral("18446744073709551615"));
}

void idlePeerSwitch()
{
	ui::TcpDemoServer first, second;
	CHECK(first.listen() && second.listen());
	Client client;
	client.connect(first);
	client.read(65536);
	client.controller.disconnectDevice();
	CHECK(!client.controller.cleanupPending());
	client.connect(second);
	client.read(65537);
	CHECK(client.scalar() == QStringLiteral("-9223372036854775808"));
	CHECK(first.connections() == 1 && second.connections() == 1);
}

void correlatedRefusalAndEof()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	Client client;
	client.connect(peer);
	client.select(65536);
	peer.setNextFieldReadUnavailable();
	const auto beforeRefusal = client.finished.count();
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return !client.controller.busy();
	}));
	CHECK(client.finished.count() == beforeRefusal + 1 && !client.finished.last().at(0).toBool());
	CHECK(client.controller.connected() && client.controller.canRefresh());
	CHECK(!client.controller.inspector().recoveryRequired() && !client.controller.cleanupPending());
	CHECK(client.values.rowCount() == 0 && peer.connections() == 1);
	client.read(65536);
	CHECK(peer.connections() == 1 && client.scalar() == QStringLiteral("18446744073709551615"));

	client.select(65537);
	peer.setCloseAfterReplyBytes(65568); // Full valid reply followed immediately by orderly EOF.
	const auto beforeEof = client.finished.count();
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return !client.controller.connected() && !client.controller.busy();
	}));
	CHECK(client.finished.count() == beforeEof + 1 && client.finished.last().at(0).toBool());
	CHECK(client.values.rowCount() == 0 && client.endpoints.rowCount() == 0);
	CHECK(!client.controller.cleanupPending());
	++reads;

	CHECK(await([&] {
		return !peer.hasClient();
	}));
	client.connect(peer);
	client.select(65536);
	client.controller.setReadTimeout(20);
	peer.setReplyDelay(100);
	peer.setCloseAfterReplyBytes(65568);
	const auto beforeLate = client.finished.count();
	CHECK(client.controller.refresh());
	CHECK(await([&] {
		return !client.controller.busy();
	}));
	CHECK(client.finished.count() == beforeLate + 1 && !client.finished.last().at(0).toBool());
	QTest::qWait(150);
	CHECK(client.finished.count() == beforeLate + 1 && client.values.rowCount() == 0);
}

void transportObjectReuse()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	ui::TcpTransport port;
	QSignalSpy descriptors{&port, &ui::TcpTransport::descriptorReceived};
	QSignalSpy packets{&port, &ui::TcpTransport::packetReceived};
	QSignalSpy errors{&port, &ui::TcpTransport::transportError};
	port.connectTo(QStringLiteral("127.0.0.1"), peer.port());
	CHECK(await([&] {
		return descriptors.count() == 1;
	}));
	CHECK(port.ready());
	const auto send = [&](quint32 session, quint32 id, quint32 endpointId) {
		namespace encoded = telemetry::protocol::encoded;
		std::array<std::byte, 32> request{};
		encoded::Header header;
		header.session = session;
		header.requestId = id;
		header.endpoint = endpointId;
		header.expectedResponseBytes = 8;
		CHECK(encoded::writeHeader(request, header) == encoded::Status::Ok);
		CHECK(port.stage({request, session, id}));
		CHECK(port.sendStaged());
	};
	send(101, 1, 65536);
	CHECK(await([&] {
		return packets.count() == 1;
	}));
	CHECK(await([&] {
		return port.canStage();
	}));
	peer.setPrependPreviousEncodedReply();
	bool closed = false;
	const auto direct =
	    QObject::connect(&port, &ui::TcpTransport::packetReceived, &port,
	                     [&](const QByteArray&, quint32 session, quint32 id) {
		                     if (!closed && session == 101 && id == 1) {
			                     closed = true;
			                     port.close();
			                     CHECK(port.locallyQuiescent());
			                     QTimer::singleShot(10, &port, [&] {
				                     port.connectTo(QStringLiteral("127.0.0.1"), peer.port());
			                     });
		                     }
	                     });
	send(101, 2, 65536);
	CHECK(await([&] {
		return descriptors.count() == 2;
	}));
	CHECK(closed && packets.count() == 2 && errors.isEmpty());
	QObject::disconnect(direct);
	CHECK(await([&] {
		return port.canStage();
	}));
	send(202, 1, 65537);
	CHECK(await([&] {
		return packets.count() == 3;
	}));
	CHECK(packets.last().at(1).toUInt() == 202 && packets.last().at(2).toUInt() == 1);
	port.close();
	CHECK(port.locallyQuiescent());
}

void discoveryRefusals()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	Client client;
	peer.setDescriptorPath(QStringLiteral("/telemetry/other.bin"));
	CHECK(client.controller.connectTcp(QStringLiteral("127.0.0.1"), peer.port()));
	CHECK(await([&] {
		return peer.connections() == 1 && !peer.hasClient();
	}));
	CHECK(!client.controller.connected() && client.endpoints.rowCount() == 0);
	CHECK(peer.resourceReadIndices().isEmpty() && peer.fieldReads() == 0);
	peer.setDescriptorPath(QStringLiteral("/telemetry/descriptor.bin"));
	peer.setDescriptorOverride(QByteArray(64, '\0'));
	CHECK(client.controller.connectTcp(QStringLiteral("127.0.0.1"), peer.port()));
	CHECK(await([&] {
		return peer.connections() == 2 && !peer.hasClient();
	}));
	CHECK(!client.controller.connected() && client.endpoints.rowCount() == 0);
	CHECK(peer.fieldReads() == 0);
	client.controller.disconnectDevice();
}

void oneClientAndInvalidStream()
{
	ui::TcpDemoServer peer;
	CHECK(peer.listen());
	Client client;
	client.connect(peer);
	QTcpSocket second;
	second.connectToHost(QHostAddress::LocalHost, peer.port());
	CHECK(await([&] {
		return second.state() == QAbstractSocket::UnconnectedState;
	}));
	CHECK(peer.connections() == 1 && peer.hasClient() && client.controller.connected());
	client.read(65536);
	client.controller.disconnectDevice();
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	QTcpSocket malformed;
	malformed.connectToHost(QHostAddress::LocalHost, peer.port());
	CHECK(await([&] {
		return malformed.state() == QAbstractSocket::ConnectedState;
	}));
	CHECK(malformed.write("\x7f", 1) == 1);
	CHECK(await([&] {
		return malformed.state() == QAbstractSocket::UnconnectedState;
	}));
	CHECK(!peer.hasClient());
}

void writableSocketCycle()
{
	ui::TcpDemoServer peer;
	peer.setWritableDemo(true);
	peer.setReplyFragmentBytes(3);
	CHECK(peer.listen());
	Client client;
	client.controller.setTcpWriteChunkBytes(3);
	client.connect(peer);
	client.read(0);
	const auto config = endpoint(client.endpoints, 1, 0);
	CHECK(client.endpoints.endpoint(config).value("writable").toBool());
	CHECK(client.controller.writeProfileSupported() && !client.controller.writesEnabled());
	CHECK(!client.controller.beginEdit() && peer.fieldWrites() == 0);
	client.controller.setWritesEnabled(true);
	QSignalSpy applied{&client.controller, &ui::InspectorController::applyFinished};
	const auto editGain = [&](const QString& value) {
		CHECK(client.controller.beginEdit());
		const auto root = client.values.index(0, 0);
		CHECK(client.controller.setScalarText(named(client.values, root, QStringLiteral("Gain")),
		                                      value));
	};
	const auto apply = [&] {
		const auto before = applied.count();
		CHECK(client.controller.apply() && !client.controller.apply());
		CHECK(await([&] {
			return applied.count() == before + 1;
		}));
		CHECK(!client.controller.busy());
	};
	editGain(QStringLiteral("25"));
	apply();
	CHECK(applied.last()[0].toBool() && peer.fieldWrites() == 1 && peer.nativeWrites() == 1 &&
	      peer.fieldReads() == 2);
	CHECK(client.values
	          .data(named(client.values, client.values.index(0, 0), QStringLiteral("Gain")),
	                ui::ValueModel::ValueRole)
	          .toString() == QStringLiteral("10"));
	CHECK(client.controller.connected() &&
	      client.controller.writeOutcome().contains(QStringLiteral("confirmed")));
	client.controller.setTcpWriteChunkBytes(0);
	++reads;
	editGain(QStringLiteral("6"));
	peer.setNextFieldReadUnavailable();
	apply();
	CHECK(!applied.last()[0].toBool() && peer.fieldWrites() == 2 && peer.fieldReads() == 3);
	CHECK(client.controller.connected() && client.values.rowCount() == 0);
	CHECK(client.controller.writeOutcome().contains(QStringLiteral("unconfirmed")));
	CHECK(client.controller.writeOutcome().contains(QStringLiteral("dispatch 9")));
	client.read(0);
	CHECK(client.values
	          .data(named(client.values, client.values.index(0, 0), QStringLiteral("Gain")),
	                ui::ValueModel::ValueRole)
	          .toString() == QStringLiteral("6"));
	editGain(QStringLiteral("7"));
	peer.setRejectWrites(true);
	const auto beforeRefusal = peer.fieldReads();
	apply();
	CHECK(!applied.last()[0].toBool() && peer.fieldWrites() == 3 &&
	      peer.fieldReads() == beforeRefusal);
	CHECK(client.controller.connected() && client.values.rowCount() == 0);
	peer.setRejectWrites(false);
	client.read(0);
	editGain(QStringLiteral("8"));
	peer.setDropNextEncodedReply();
	client.controller.setReadTimeout(80);
	apply();
	CHECK(!applied.last()[0].toBool() && peer.fieldWrites() == 4 && peer.nativeWrites() == 4);
	CHECK(!client.controller.connected() && client.values.rowCount() == 0);
	CHECK(client.controller.writeOutcome().contains(QStringLiteral("OutcomeUnknown")));
	QTest::qWait(120);
	client.controller.pump();
	CHECK(peer.fieldWrites() == 4 && peer.nativeWrites() == 4 && applied.count() == 4);
	CHECK(await([&] {
		return !peer.hasClient();
	}));
	client.connect(peer);
	CHECK(!client.controller.cleanupPending() && !client.controller.writesEnabled());
	CHECK(peer.nativeWrites() == 4); // Fresh bootstrap did not replay the uncertain mutation.
	client.read(0);
	client.controller.setWritesEnabled(true);
	editGain(QStringLiteral("9"));
	peer.setReplyFragmentBytes(0);
	peer.setCloseAfterReplyBytes(
	    32); // A complete write ACK precedes EOF; readback remains unconfirmed.
	apply();
	CHECK(!applied.last()[0].toBool() && peer.fieldWrites() == 5);
	CHECK(client.controller.writeOutcome().contains(QStringLiteral("Write applied")));
	CHECK(!client.controller.writeOutcome().contains(QStringLiteral("OutcomeUnknown")));
	CHECK(client.values.rowCount() == 0);
}
} // namespace

int main(int argc, char** argv)
{
	QGuiApplication application{argc, argv};
	realSocketReads();
	connectionBoundaries();
	descriptorReplacement();
	failuresAndReconnect();
	idlePeerSwitch();
	correlatedRefusalAndEof();
	transportObjectReuse();
	discoveryRefusals();
	oneClientAndInvalidStream();
	writableSocketCycle();
	std::printf("Inspector TCP checks=%u reads=%u connections=%u Qt=%s\n", checks, reads,
	            connections, qVersion());
	return 0;
}
