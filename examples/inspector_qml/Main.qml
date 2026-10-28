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
            Label { text: "READ ONLY"; color: "#99abbf"; font.pixelSize: 12 }
            Button { text: "Demo device"; onClicked: controller.connectDemo() }
            Button { text: "Disconnect"; enabled: controller.connected; onClicked: controller.disconnectDevice() }
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
                                    controller.selectEndpoint(endpointTree.index(row, 0))
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
                            objectName: "valueRow"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            implicitWidth: valueTree.width
                            implicitHeight: 40
                            text: name
                            ToolTip.visible: hovered && detail.length > 0
                            ToolTip.text: detail
                            contentItem: RowLayout {
                                Label { text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: value; color: "#83e1ce"; font.family: "monospace" }
                                Button {
                                    visible: isAggregate && childCount > 128
                                    enabled: loadedCount < childCount
                                    text: "Load next 128 · " + loadedCount + "/" + childCount
                                    onClicked: controller.values.fetchMore(valueTree.index(row, 0))
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
                        text: "Values belong to the last completed read. Arrays load in batches as you expand them."
                        color: "#99abbf"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
        Label {
            objectName: "statusLabel"
            text: controller.statusText
            color: controller.connected ? "#83e1ce" : "#99abbf"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            text: "Demo uses a local Telemetry Model and real Encoded packets. No physical device is connected."
            color: "#718297"
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.Wrap
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
