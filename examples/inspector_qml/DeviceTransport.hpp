/*
 * @file DeviceTransport.hpp
 * @brief Application transport boundary for the Inspector owner loop.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_DEVICE_TRANSPORT_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_DEVICE_TRANSPORT_HPP
#pragma once

#include <client/Backend.hpp>
#include <QtCore/QByteArray>
#include <QtCore/QObject>

namespace inspector_qml {
class DeviceTransport : public QObject {
	Q_OBJECT
public:
	explicit DeviceTransport(QObject* parent = nullptr) : QObject(parent)
	{}

	~DeviceTransport() override = default;
	virtual void close() = 0;
	virtual bool ready() const noexcept = 0;
	virtual bool canStage() const noexcept = 0;
	virtual bool stage(telemetry::client::Packet packet) = 0;
	virtual bool sendStaged() = 0;
	virtual void discardStaged() = 0;
	virtual bool locallyQuiescent() const noexcept = 0;
	virtual QString peerAddress() const = 0;
	virtual void setWriteChunkBytes(int bytes) noexcept = 0;
	virtual void setTimeout(int milliseconds) noexcept = 0;
Q_SIGNALS:
	void descriptorReceived(QByteArray bytes);
	void packetAccepted(quint32 session, quint32 requestId);
	void packetReceived(QByteArray bytes, quint32 session, quint32 requestId);
	void transportError(QString message);
};
} // namespace inspector_qml
#endif
