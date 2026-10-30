// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "inspectorWindow"
    visible: true
    width: 1080
    height: 720
    minimumWidth: 780
    minimumHeight: 480
    title: "Telemetry Inspector"
    color: "#11151c"
    palette.window: "#11151c"
    palette.windowText: "#e8edf5"
    palette.base: "#17202b"
    palette.alternateBase: "#1c2836"
    palette.text: "#e8edf5"
    palette.button: "#233349"
    palette.buttonText: "#e8edf5"
    palette.highlight: "#215a68"
    palette.highlightedText: "#ffffff"
    palette.placeholderText: "#718297"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Label { text: "TELEMETRY"; color: "#68d6c2"; font.pixelSize: 12; font.letterSpacing: 2 }
                Label { text: "Universal Inspector"; font.pixelSize: 28; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label { text: controller.editing ? "LOCAL DRAFT" : controller.writesEnabled ? "FIELD EDITING" : "READ ONLY"; color: "#99abbf"; font.pixelSize: 12 }
            Button { text: "Demo device"; onClicked: controller.connectDemo() }
            Button { objectName: "writableDemoButton"; text: "Writable demo"; onClicked: controller.connectWritableDemo() }
            Button { text: "Disconnect"; enabled: controller.connected || controller.busy; onClicked: controller.disconnectDevice() }
        }
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                text: "Enable Field writes for this connection"
                checked: controller.writesEnabled
                enabled: controller.writeProfileSupported && !controller.busy
                onToggled: controller.writesEnabled = checked
            }
            Label {
                visible: controller.connected && !controller.writeProfileSupported
                text: "This transport profile is read-only."
                color: "#99abbf"
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: tcpHost
                objectName: "tcpHost"
                Layout.fillWidth: true
                text: controller.tcpHost
                placeholderText: "TCP host"
                onAccepted: controller.connectTcp(text, tcpPort.value)
            }
            SpinBox {
                id: tcpPort
                objectName: "tcpPort"
                from: 1
                to: 65535
                value: controller.tcpPort
                editable: true
            }
            Button {
                text: "Connect TCP"
                enabled: tcpHost.text.length > 0 && !controller.busy
                onClicked: controller.connectTcp(tcpHost.text, tcpPort.value)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: controller.serialAvailable
            ComboBox {
                id: serialPort
                objectName: "serialPort"
                Layout.fillWidth: true
                editable: true
                model: controller.serialPorts()
                editText: controller.serialPort
            }
            Button {
                text: "Scan ports"
                onClicked: serialPort.model = controller.serialPorts()
            }
            SpinBox {
                id: serialBaud
                from: 1200
                to: 4000000
                value: controller.serialBaud
                editable: true
            }
            Button {
                text: "Connect UART"
                enabled: serialPort.editText.length > 0 && !controller.busy
                onClicked: controller.connectSerial(serialPort.editText, serialBaud.value)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: descriptorPath
                objectName: "descriptorPath"
                Layout.fillWidth: true
                placeholderText: "Descriptor file path · metadata only"
                onAccepted: controller.openDescriptorPath(text)
            }
            Button {
                text: "Open Descriptor"
                enabled: descriptorPath.text.length > 0
                onClicked: controller.openDescriptorPath(descriptorPath.text)
            }
        }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            Pane {
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 220
                background: Rectangle { color: "#17202b"; radius: 10 }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label { text: "DEVICE ENDPOINTS"; color: "#99abbf"; font.pixelSize: 11; font.letterSpacing: 1 }
                    TreeView {
                        id: endpointTree
                        objectName: "endpointTree"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: controller.endpoints
                        columnWidthProvider: function() { return Math.max(1, endpointTree.width) }
                        onWidthChanged: Qt.callLater(function() { endpointTree.forceLayout() })
                        delegate: TreeViewDelegate {
                            objectName: "endpointRow"
                            required property string name
                            required property string detail
                            required property bool isEndpoint
                            implicitWidth: endpointTree.width
                            implicitHeight: 38
                            text: name
                            ToolTip.visible: hovered && detail.length > 0
                            ToolTip.text: detail
                            onClicked: {
                                if (isEndpoint)
                                    controller.selectEndpoint(endpointTree.modelIndex(Qt.point(0, row)))
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Component.onCompleted: Qt.callLater(function() {
                            endpointTree.forceLayout()
                            endpointTree.expandRecursively(0, 2)
                        })
                    }
                    Label { text: "Commands and Services show metadata only."; color: "#99abbf"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
                }
            }
            Pane {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                background: Rectangle { color: "#17202b"; radius: 10 }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: controller.selection.length ? controller.selection : "Select a Field"
                            font.pixelSize: 21
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                        BusyIndicator { running: controller.busy; visible: running; implicitWidth: 30; implicitHeight: 30 }
                        Button {
                            objectName: "editButton"
                            text: "Edit"
                            visible: !controller.editing
                            enabled: controller.canEdit
                            onClicked: controller.beginEdit()
                        }
                        Button {
                            objectName: "cancelEditButton"
                            text: "Cancel"
                            visible: controller.editing
                            enabled: !controller.busy
                            onClicked: controller.cancelEdit()
                        }
                        Button {
                            objectName: "applyButton"
                            text: "Apply + Refresh"
                            visible: controller.editing
                            enabled: controller.canApply
                            onClicked: controller.apply()
                        }
                        Button {
                            objectName: "refreshButton"
                            text: "Refresh"
                            enabled: controller.canRefresh
                            onClicked: controller.refresh()
                        }
                    }
                    TreeView {
                        id: valueTree
                        objectName: "valueTree"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: controller.values
                        columnWidthProvider: function() { return Math.max(1, valueTree.width) }
                        onWidthChanged: Qt.callLater(function() { valueTree.forceLayout() })
                        delegate: TreeViewDelegate {
                            id: valueDelegate
                            objectName: "valueRow"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            required property bool editable
                            required property string editorText
                            implicitWidth: valueTree.width
                            implicitHeight: 40
                            text: name
                            ToolTip.visible: hovered && detail.length > 0
                            ToolTip.text: detail
                            contentItem: RowLayout {
                                Label { text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { visible: !editable; text: value; color: "#83e1ce"; font.family: "monospace" }
                                TextField {
                                    objectName: "scalarEditor"
                                    property string scalarName: valueDelegate.name
                                    visible: valueDelegate.editable
                                    Layout.preferredWidth: 230
                                    text: valueDelegate.editorText
                                    font.family: "monospace"
                                    selectByMouse: true
                                    onEditingFinished: controller.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                }
                                Button {
                                    visible: isAggregate && childCount > 128
                                    enabled: loadedCount < childCount
                                    text: "Load next 128 · " + loadedCount + "/" + childCount
                                    onClicked: controller.values.fetchMore(valueTree.modelIndex(Qt.point(0, row)))
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Label {
                            anchors.centerIn: parent
                            visible: valueTree.rows === 0
                            text: controller.busy ? "Reading the selected Field…" : "Select a Field, then press Refresh."
                            color: "#99abbf"
                        }
                    }
                    Label {
                        text: controller.editing
                            ? "Local draft only. Press Enter after a scalar edit. Apply writes once, then reads the device again."
                            : "Values belong to the last completed read. Arrays load in batches as you expand them."
                        color: "#99abbf"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
        Label {
            objectName: "writeOutcomeLabel"
            visible: controller.writeOutcome.length > 0
            text: controller.writeOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "statusLabel"
            text: controller.statusText
            color: controller.connected ? "#83e1ce" : "#99abbf"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            text: controller.serialAvailable
                ? "Connect TCP or UART to read a device. Demo device uses simulated values."
                : "Connect TCP to read a device. Demo device uses simulated values."
            color: "#718297"
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: controller.values
        function onModelReset() {
            Qt.callLater(function() {
                valueTree.forceLayout()
                valueTree.expand(0)
            })
        }
    }
    Connections {
        target: controller.endpoints
        function onModelReset() {
            Qt.callLater(function() { endpointTree.expandRecursively(0, 2) })
        }
    }
    Connections {
        target: controller
        function onReadFinished(published) {
            if (published)
                Qt.callLater(function() {
                    valueTree.forceLayout()
                    valueTree.expand(0)
                })
        }
    }
}
