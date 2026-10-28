/*
 * @file InspectorController.hpp
 * @brief Qt owner loop and descriptor storage for the read-only inspector application.
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
#include <memory>

namespace inspector_qml {

class InspectorController final : public QObject {

	Q_OBJECT
	Q_PROPERTY(QAbstractItemModel* endpoints READ endpoints CONSTANT)
	Q_PROPERTY(QAbstractItemModel* values READ values CONSTANT)
	Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	Q_PROPERTY(bool canRefresh READ canRefresh NOTIFY stateChanged)
	Q_PROPERTY(QString selection READ selection NOTIFY stateChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)

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
		return busy_;
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

	Q_INVOKABLE bool connectDemo();
	Q_INVOKABLE bool loadDescriptor(const QUrl& path);
	Q_INVOKABLE bool openDescriptorPath(const QString& path);
	Q_INVOKABLE bool selectEndpoint(const QModelIndex& index);
	Q_INVOKABLE bool refresh();
	Q_INVOKABLE void disconnectDevice();

	// Host-port controls also let the Qt tests exercise actual asynchronous reads.
	void pump();
	void setReplyDelay(int milliseconds) noexcept;
	void setDropReplies(bool drop) noexcept;
	void setReadTimeout(int milliseconds) noexcept;
	QByteArray demoDescriptor() const;
	Inspector& inspector() noexcept;

Q_SIGNALS:
	void stateChanged();
	void readFinished(bool published);

private:
	struct State;
	std::unique_ptr<State> state_;
	EndpointModel endpoints_;
	ValueModel values_;
	QTimer timer_;
	QString selection_, status_ = QStringLiteral("Connect the demo or open a Descriptor file.");
	quint32 session_ = 0;
	quint64 epoch_ = 0;
	int replyDelay_ = 0, readTimeout_ = 3000;
	bool connected_ = false, busy_ = false, dropReplies_ = false, updating_ = false;
	bool replaceDescriptor(QByteArray bytes, bool demo);
	void retire();
};

} // namespace inspector_qml
#endif
