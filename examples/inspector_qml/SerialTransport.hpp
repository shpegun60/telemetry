/*
 * @file SerialTransport.hpp
 * @brief Qt SerialPort and existing COBS endpoint for the Inspector owner loop.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_SERIAL_TRANSPORT_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_SERIAL_TRANSPORT_HPP
#pragma once

#include "DeviceTransport.hpp"
#include <memory>

namespace inspector_qml {
class SerialTransport final : public DeviceTransport {
public:
	explicit SerialTransport(QObject* parent = nullptr);
	~SerialTransport() override;
	void connectTo(const QString& port, qint32 baud = 115200);
	// Opt-in configuration for the next connect; ordinary Inspector discovery
	// remains Descriptor-only. The returned capability is owned and ready-gated.
	void setReadManyDiscovery(bool enabled) noexcept;
	QByteArray readManyCapability() const;
	void close() override;
	bool ready() const noexcept override;
	bool canStage() const noexcept override;
	bool stage(telemetry::client::Packet packet) override;
	bool sendStaged() override;
	void discardStaged() override;
	bool locallyQuiescent() const noexcept override;
	QString peerAddress() const override;
	void setWriteChunkBytes(int bytes) noexcept override;
	// Explicit absolute deadline for future exchanges; overrides automatic airtime budgeting.
	void setTimeout(int milliseconds) noexcept override;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace inspector_qml
#endif // TELEMETRY_EXAMPLES_INSPECTOR_QML_SERIAL_TRANSPORT_HPP
