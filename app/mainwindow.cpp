#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QComboBox>

#include <cmath>

#include "serialization/TelemetryJson.h"
#include "serialization/TelemetryCommandJson.h"

namespace {

QString scalarText(const telemetry::Field& field)
{
    const auto value = field.read();
    using telemetry::ScalarType;
    switch (value.type()) {
        case ScalarType::F32:
            return std::isfinite(value.get<float>()) ? QString::number(value.get<float>(), 'g', 7) : "n/a";
        case ScalarType::F64:
            return std::isfinite(value.get<double>()) ? QString::number(value.get<double>(), 'g', 17) : "n/a";
        case ScalarType::U8: return QString::number(static_cast<unsigned>(value.get<std::uint8_t>()));
        case ScalarType::U16: return QString::number(static_cast<unsigned>(value.get<std::uint16_t>()));
        case ScalarType::U32: return QString::number(value.get<std::uint32_t>());
        case ScalarType::U64: return QString::number(static_cast<qulonglong>(value.get<std::uint64_t>()));
        case ScalarType::S8: return QString::number(static_cast<int>(value.get<std::int8_t>()));
        case ScalarType::S16: return QString::number(static_cast<int>(value.get<std::int16_t>()));
        case ScalarType::S32: return QString::number(value.get<std::int32_t>());
        case ScalarType::S64: return QString::number(static_cast<qlonglong>(value.get<std::int64_t>()));
        case ScalarType::Bool: return value.get<bool>() ? "true" : "false";
        case ScalarType::Null: return "n/a";
        default: return "n/a";
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Telemetry playground");

    auto* layout = new QVBoxLayout(ui->centralwidget);
    layout->addWidget(new QLabel("Typed fields and commands: signatures define their parameter types.", this));

    fields_ = new QTableWidget(this);
    fields_->setColumnCount(5);
    fields_->setHorizontalHeaderLabels({"ID", "Field", "Type", "Unit", "Value"});
    fields_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    fields_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(fields_);

    char schema[4096];
    const auto length = telemetry::writeSchema(demo::fieldIndex, schema, sizeof(schema));
    const auto document = QJsonDocument::fromJson(QByteArray(schema, static_cast<int>(length)));
    for (const auto& entry : document.object().value("catalogs").toArray()) {
        const auto catalog = entry.toObject();
        for (const auto& fieldEntry : catalog.value("fields").toArray()) {
            const auto field = fieldEntry.toObject();
            const int row = fields_->rowCount();
            fields_->insertRow(row);
            fields_->setItem(row, 0, new QTableWidgetItem(QString::number(field.value("id").toInteger())));
            fields_->setItem(row, 1, new QTableWidgetItem(catalog.value("name").toString() + "." + field.value("n").toString()));
            fields_->setItem(row, 2, new QTableWidgetItem(field.value("t").toString()));
            fields_->setItem(row, 3, new QTableWidgetItem(field.value("u").toString()));
            fields_->setItem(row, 4, new QTableWidgetItem);
        }
    }

    auto* schemaText = new QPlainTextEdit(this);
    schemaText->setReadOnly(true);
    // Keep full U64/S64 metadata digits. Parsing into a QJsonDocument is
    // useful for table structure, but reserializing it can round U64 extrema.
    schemaText->setPlainText(length == 0 ? "Schema buffer is too small" :
                            QString::fromUtf8(schema, static_cast<int>(length)));
    schemaText->setMaximumHeight(110);
    layout->addWidget(new QLabel("Schema JSON", this));
    layout->addWidget(schemaText);

    values_ = new QPlainTextEdit(this);
    values_->setReadOnly(true);
    values_->setMaximumHeight(80);
    layout->addWidget(new QLabel("Values JSON", this));
    layout->addWidget(values_);

    const auto commandLength = telemetry::writeSchema(demo::commandIndex, schema, sizeof(schema));
    auto* commandSchema = new QPlainTextEdit(this);
    commandSchema->setReadOnly(true);
    commandSchema->setMaximumHeight(90);
    commandSchema->setPlainText(commandLength ? QString::fromUtf8(schema, static_cast<int>(commandLength))
                                             : "Command schema buffer is too small");
    layout->addWidget(new QLabel("Commands", this));
    layout->addWidget(commandSchema);
    auto* controls = new QHBoxLayout;
    auto* reset = new QPushButton("Reset counter", this);
    auto* limit = new QDoubleSpinBox(this);
    limit->setRange(1.0, 1000.0);
    limit->setValue(250.0);
    limit->setSuffix(" V");
    auto* mode = new QComboBox(this);
    mode->addItems({"Off", "Auto", "Manual"});
    mode->setCurrentIndex(1);
    auto* apply = new QPushButton("Configure meter", this);
    auto* result = new QLabel(this);
    controls->addWidget(reset);
    controls->addWidget(limit);
    controls->addWidget(mode);
    controls->addWidget(apply);
    controls->addWidget(result);
    layout->addLayout(controls);
    connect(reset, &QPushButton::clicked, this, [this, result] {
        const auto status = demo::commandIndex.call(0);
        result->setText(status == telemetry::CommandResult::Executed ? "Executed" : "Not executed");
        refreshValues();
    });
    connect(apply, &QPushButton::clicked, this, [this, result, limit, mode] {
        const auto status = demo::commands.call<1>(limit->value(), mode->currentIndex());
        result->setText(status == telemetry::CommandResult::Executed ? "Executed" : "Invalid arguments");
        refreshValues();
    });

    auto* timer = new QTimer(this);
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, [this] {
        demo::advance();
        refreshValues();
    });
    auto* pause = new QPushButton("Pause", this);
    connect(pause, &QPushButton::clicked, this, [timer, pause] {
        if (timer->isActive()) {
            timer->stop();
            pause->setText("Resume");
        } else {
            timer->start();
            pause->setText("Pause");
        }
    });
    layout->addWidget(pause);
    refreshValues();
    timer->start();
}

void MainWindow::refreshValues()
{
    char values[1024];
    const auto length = telemetry::writeValues(demo::fieldIndex, values, sizeof(values));
    if (length == 0) {
        values_->setPlainText("Values buffer is too small");
        return;
    }
    const QByteArray json(values, static_cast<int>(length));
    values_->setPlainText(QString::fromUtf8(json));
    int row = 0;
    for (std::size_t c = 0; c < demo::fields.size(); ++c) {
        const auto& catalog = demo::fields.data()[c];
        for (std::size_t i = 0; i < catalog.count; ++i) {
            if (auto* item = fields_->item(row++, 4)) {
                // The simulated sources are stable during this refresh. Reading
                // them directly keeps U64/S64 out of floating-point conversion.
                item->setText(scalarText(catalog.fields[i]));
            }
        }
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
