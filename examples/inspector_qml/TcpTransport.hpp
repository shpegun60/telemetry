/*
 * @file TcpTransport.hpp
 * @brief Qt TCP ownership, Resource discovery and unchanged Encoded packet transport.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_TCP_TRANSPORT_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_TCP_TRANSPORT_HPP
#pragma once

#include <client/Backend.hpp>
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <memory>

namespace inspector_qml {
class TcpTransport final : public QObject {

	Q_OBJECT

public:
	explicit TcpTransport(QObject* parent = nullptr);
	~TcpTransport() override;
	void connectTo(const QString& host, quint16 port);
	void close();
	bool ready() const noexcept;
	bool canStage() const noexcept;
	bool stage(telemetry::client::Packet packet);
	bool sendStaged();
	void discardStaged();
	bool locallyQuiescent() const noexcept;
	QString peerAddress() const;
	void setWriteChunkBytes(int bytes) noexcept;
	void setTimeout(int milliseconds) noexcept;

Q_SIGNALS:
	void descriptorReceived(QByteArray bytes);
	void packetAccepted(quint32 session, quint32 requestId);
	void packetReceived(QByteArray bytes, quint32 session, quint32 requestId);
	void transportError(QString message);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace inspector_qml
#endif
