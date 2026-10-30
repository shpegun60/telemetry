/*
 * @file InspectorController.hpp
 * @brief Qt owner loop for runtime inspection and explicit Field/Command actions.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#ifndef TELEMETRY_EXAMPLES_INSPECTOR_QML_INSPECTOR_CONTROLLER_HPP
#define TELEMETRY_EXAMPLES_INSPECTOR_QML_INSPECTOR_CONTROLLER_HPP
#pragma once

#include "Models.hpp"
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QStringList>
#include <QtCore/QSet>
#include <memory>
#include <optional>
#include <utility>

namespace inspector_qml {
class DeviceTransport;

class InspectorController final : public QObject {

	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY stateChanged)
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
	Q_PROPERTY(QString tcpHost READ tcpHost NOTIFY stateChanged)
	Q_PROPERTY(quint16 tcpPort READ tcpPort NOTIFY stateChanged)
	Q_PROPERTY(bool serialAvailable READ serialAvailable CONSTANT)
	Q_PROPERTY(QString serialPort READ serialPort NOTIFY stateChanged)
	Q_PROPERTY(qint32 serialBaud READ serialBaud NOTIFY stateChanged)
	Q_PROPERTY(bool writesEnabled READ writesEnabled WRITE setWritesEnabled NOTIFY stateChanged)
	Q_PROPERTY(bool writeProfileSupported READ writeProfileSupported NOTIFY stateChanged)
	Q_PROPERTY(bool canEdit READ canEdit NOTIFY stateChanged)
	Q_PROPERTY(bool editing READ editing NOTIFY stateChanged)
	Q_PROPERTY(bool canApply READ canApply NOTIFY stateChanged)
	Q_PROPERTY(QString writeOutcome READ writeOutcome NOTIFY stateChanged)
	Q_PROPERTY(bool commandSelected READ commandSelected NOTIFY stateChanged)
	Q_PROPERTY(bool canExecute READ canExecute NOTIFY stateChanged)
	Q_PROPERTY(QString commandOutcome READ commandOutcome NOTIFY stateChanged)

public:
	explicit InspectorController(QObject* parent = nullptr);
	~InspectorController() override;

	QAbstractItemModel* endpoints() noexcept
	{
		return &endpoints_;
	}

	QAbstractItemModel* values() noexcept
	{
		return &values_;
	}

	bool connected() const noexcept
	{
		return connected_;
	}

	bool busy() const noexcept
	{
		return busy_ || connecting_;
	}

	bool canRefresh() const noexcept;

	QString selection() const
	{
		return selection_;
	}

	QString statusText() const
	{
		return status_;
	}

	QString tcpHost() const
	{
		return peerHost_;
	}

	quint16 tcpPort() const noexcept
	{
		return peerPort_;
	}

	bool cleanupPending() const noexcept
	{
		return bool(retiredState_);
	}

	bool serialAvailable() const noexcept;

	QString serialPort() const
	{
		return serialPort_;
	}

	qint32 serialBaud() const noexcept
	{
		return serialBaud_;
	}

	Q_INVOKABLE QStringList serialPorts() const;
	Q_INVOKABLE bool connectSerial(const QString& port, qint32 baud = 115200);
	void setSerialWriteChunkBytes(int bytes) noexcept;
	Q_INVOKABLE bool connectDemo();
	Q_INVOKABLE bool connectWritableDemo();
	Q_INVOKABLE bool connectTcp(const QString& host, quint16 port = 5577);
	Q_INVOKABLE bool loadDescriptor(const QUrl& path);
	Q_INVOKABLE bool openDescriptorPath(const QString& path);
	Q_INVOKABLE bool selectEndpoint(const QModelIndex& index);
	Q_INVOKABLE bool refresh();
	Q_INVOKABLE bool beginEdit();
	Q_INVOKABLE void cancelEdit();
	Q_INVOKABLE bool setScalarText(const QModelIndex& index, const QString& text);
	Q_INVOKABLE bool apply();
	Q_INVOKABLE bool executeCommand();
	void setWritesEnabled(bool enabled);

	bool writesEnabled() const noexcept
	{
		return writesEnabled_;
	}

	bool writeProfileSupported() const noexcept;
	bool canEdit() const noexcept;
	bool editing() const noexcept;
	bool canApply() const noexcept;
	bool commandSelected() const noexcept;
	bool canExecute() const noexcept;
	QString commandOutcome() const;

	QString writeOutcome() const
	{
		if (writeOutcome_.isEmpty() || writeOutcomeSession_ == 0 ||
		    writeOutcomeSession_ == session_)
			return writeOutcome_;
		return QStringLiteral("Previous session %1 (current session %2) · %3")
		    .arg(writeOutcomeSession_)
		    .arg(session_)
		    .arg(writeOutcome_);
	}

	Q_INVOKABLE void disconnectDevice();

	// Host-port controls also let the Qt tests exercise actual asynchronous reads.
	void pump();
	void setReplyDelay(int milliseconds) noexcept;
	void setDropReplies(bool drop) noexcept;
	void setRejectWrites(bool reject) noexcept;
	quint64 demoWriteCount() const noexcept;
	quint64 demoCommandCount() const noexcept;
	quint64 demoExecutedCount() const noexcept;
	quint64 demoAcceptedCount() const noexcept;
	quint64 demoCommandCounter() const noexcept;
	float demoCommandGain() const noexcept;
	// Explicit enqueue deadline, including for serial reads; does not add an airtime allowance.
	void setReadTimeout(int milliseconds) noexcept;
	void setTcpWriteChunkBytes(int bytes) noexcept;
	QByteArray demoDescriptor() const;
	Workbench& inspector() noexcept;

Q_SIGNALS:
	void stateChanged();
	void readFinished(bool published);
	void applyFinished(bool confirmed);
	void commandFinished(bool executed, bool accepted);

private:
	struct Completion {
		std::optional<bool> applied;
		std::optional<std::pair<bool, bool>> command;
	};
	struct State;
	std::unique_ptr<State> state_;
	std::unique_ptr<State> retiredState_;
	std::unique_ptr<DeviceTransport> transport_;
	EndpointModel endpoints_;
	ValueModel values_;
	QTimer timer_;
	QString selection_, status_ = QStringLiteral("Connect the demo or open a Descriptor file.");
	QString peerHost_ = QStringLiteral("10.50.0.0"), peerAddress_;
	QString retiredPeerHost_, retiredPeerAddress_;
	quint16 peerPort_ = 5577, retiredPeerPort_ = 0;
	QString serialPort_, retiredSerialPort_;
	qint32 serialBaud_ = 115200, retiredSerialBaud_ = 0;
	bool serial_ = false, retiredSerial_ = false;
	int serialWriteChunk_ = 0;
	quint32 session_ = 0;
	quint64 epoch_ = 0;
	int replyDelay_ = 0, readTimeout_ = 3000;
	bool readTimeoutExplicit_ = false;
	int tcpWriteChunk_ = 0;
	bool connecting_ = false;
	bool connected_ = false, busy_ = false, dropReplies_ = false, updating_ = false;
	bool writesEnabled_ = false;
	QString writeOutcome_;
	quint32 writeOutcomeSession_ = 0;
	QString commandOutcome_;
	quint32 commandOutcomeSession_ = 0;
	QSet<quintptr> invalidEditors_;
	bool replaceDescriptor(QByteArray bytes, bool demo, bool remote = false,
	                       bool writableDemo = false);
	Completion retire();
	std::optional<bool> consumeApply(State& state, bool displaySnapshot);
	std::optional<std::pair<bool, bool>> consumeCommand(State& state, bool live);
	void notifyState(Completion completion, std::optional<bool> read = {});
	void bindTransportSignals();
	void transportFailed(quint64 epoch, QString error);
	void transportDescriptor(quint64 epoch, QByteArray bytes);
};

} // namespace inspector_qml
#endif
