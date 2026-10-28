/*
 * @file main.cpp
 * @brief Launch the read-only runtime QML inspector or qualify its actual window and transport.
 * @author Ruslan Kovtun (shpegun60), codexAi
 * SPDX-License-Identifier: MIT
 */
#include "InspectorController.hpp"
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQuickControls2/QQuickStyle>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickItem>
#include <algorithm>
#include <cstdio>

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Basic"));
	QCoreApplication::setApplicationName(QStringLiteral("Telemetry Inspector"));
	QCommandLineParser options;
	options.setApplicationDescription(QStringLiteral("Read-only runtime Telemetry Inspector"));
	options.addHelpOption();
	options.addOption({QStringLiteral("smoke"),
	                   QStringLiteral("Read a real demo Field and verify the QML window.")});
	options.addOption({QStringLiteral("screenshot"),
	                   QStringLiteral("Save the smoke-test window as a PNG."),
	                   QStringLiteral("file")});
	options.addOption({QStringLiteral("descriptor"),
	                   QStringLiteral("Open a Descriptor file without connecting a transport."),
	                   QStringLiteral("file")});
	options.addOption({QStringLiteral("write-demo-descriptor"),
	                   QStringLiteral("Export the demo Descriptor and exit."),
	                   QStringLiteral("file")});
	options.process(app);
	inspector_qml::InspectorController controller;
	if (options.isSet(QStringLiteral("write-demo-descriptor"))) {
		QFile output(options.value(QStringLiteral("write-demo-descriptor")));
		const auto bytes = controller.demoDescriptor();
		return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() ? 0 : 1;
	}
	const bool connected = options.isSet(QStringLiteral("descriptor"))
	                           ? controller.loadDescriptor(QUrl::fromLocalFile(
	                                 options.value(QStringLiteral("descriptor"))))
	                           : controller.connectDemo();
	if (!connected) {
		std::fprintf(stderr, "%s\n", qPrintable(controller.statusText()));
		return 1;
	}
	bool qmlWarnings = false;
	QQmlApplicationEngine engine;
	QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError>&) {
		qmlWarnings = true;
	});
	engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
	engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Telemetry/Inspector/Main.qml")));
	if (engine.rootObjects().size() != 1)
		return 2;
	QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
	                 &inspector_qml::InspectorController::disconnectDevice);
	if (options.isSet(QStringLiteral("smoke"))) {
		QObject::connect(
		    &controller, &inspector_qml::InspectorController::readFinished, &app,
		    [&](bool published) {
			    if (!published || engine.rootObjects().size() != 1 ||
			        controller.values()->rowCount() != 1) {
				    app.exit(3);
				    return;
			    }
			    QTimer::singleShot(150, &app, [&] {
				    QList<QQuickItem*> endpointItems, valueItems;
				    const auto collect = [&](auto&& self, QQuickItem* item) -> void {
					    if (item->objectName() == QStringLiteral("endpointRow"))
						    endpointItems.push_back(item);
					    if (item->objectName() == QStringLiteral("valueRow"))
						    valueItems.push_back(item);
					    for (auto* child : item->childItems())
						    self(self, child);
				    };
				    auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
				    if (mainWindow)
					    collect(collect, mainWindow->contentItem());
				    const auto visible = [](const QQuickItem* item) {
					    return item->isVisible() && item->width() > 0 && item->height() > 0;
				    };
				    const auto endpointRows =
				        std::count_if(endpointItems.begin(), endpointItems.end(), visible);
				    const auto valueRows =
				        std::count_if(valueItems.begin(), valueItems.end(), visible);
				    const bool voltage = std::any_of(
				        valueItems.begin(), valueItems.end(), [&](const QQuickItem* item) {
					        return visible(item) &&
					               item->property("name").toString() == QStringLiteral("Ua") &&
					               item->property("value").toString().startsWith(
					                   QStringLiteral("230.1"));
				        });
				    if (endpointRows == 0 || valueRows < 4 || !voltage) {
					    std::fprintf(
					        stderr,
					        "Inspector delegates missing: endpoints=%d values=%d voltage=%d\n",
					        int(endpointRows), int(valueRows), int(voltage));
					    app.exit(8);
					    return;
				    }
				    if (qmlWarnings) {
					    app.exit(7);
					    return;
				    }
				    if (options.isSet(QStringLiteral("screenshot"))) {
					    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
					    if (!window || !window->grabWindow().save(
					                       options.value(QStringLiteral("screenshot")))) {
						    app.exit(6);
						    return;
					    }
				    }
				    std::printf(
				        "Inspector QML smoke: published=1 roots=1 endpointRows=%d valueRows=%d\n",
				        int(endpointRows), int(valueRows));
				    app.exit(0);
			    });
		    });
		QTimer::singleShot(0, &controller, [&] {
			auto* model = controller.endpoints();
			const auto fieldGroup = model->index(0, 0);
			const auto catalog = model->index(0, 0, fieldGroup);
			if (!controller.selectEndpoint(model->index(0, 0, catalog)) || !controller.refresh())
				app.exit(4);
		});
		QTimer::singleShot(5000, &app, [&app] {
			app.exit(5);
		});
	}
	return app.exec();
}
