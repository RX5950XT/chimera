import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Chimera.UI 1.0

ApplicationWindow {
    id: root
    visible: true
    title: "Chimera"
    width: 1280
    height: 720
    color: "#1a1a1a"

    // Guest display area
    GuestDisplay {
        id: guestDisplay
        objectName: "guestDisplay"
        anchors.fill: parent
    }

    // Input mapper overlay (editable control hints)
    InputMapperOverlay {
        id: inputOverlay
        anchors.fill: parent
        visible: false
        opacity: 0.6
    }

    // Top toolbar
    Rectangle {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: "#2d2d2d"
        visible: !root.fullscreen

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 12

            Button {
                text: "Input Mapper"
                onClicked: inputOverlay.visible = !inputOverlay.visible
            }
            Button {
                text: "Multi-Instance"
                onClicked: multiInstanceDialog.open()
            }
            Button {
                text: "Macro"
                onClicked: macroDialog.open()
            }
            Button {
                text: ScreenRecorder.recording ? "Stop Recording" : "Record"
                onClicked: {
                    if (ScreenRecorder.recording) {
                        ScreenRecorder.stopRecording();
                    } else {
                        var now = new Date();
                        var ts = now.getFullYear() + "-" +
                                 String(now.getMonth()+1).padStart(2, '0') + "-" +
                                 String(now.getDate()).padStart(2, '0') + "_" +
                                 String(now.getHours()).padStart(2, '0') + "-" +
                                 String(now.getMinutes()).padStart(2, '0') + "-" +
                                 String(now.getSeconds()).padStart(2, '0');
                        ScreenRecorder.startRecording("recordings/chimera_" + ts + ".mp4", 10);
                    }
                }
            }
            Button {
                text: "Screenshot"
                onClicked: root.takeScreenshot()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: MacroEngine.recording ? "● REC" : (MacroEngine.playing ? "▶ PLAY" : "")
                color: MacroEngine.recording ? "red" : "#4caf50"
                font.bold: true
                visible: MacroEngine.recording || MacroEngine.playing
            }
            Label {
                text: "FPS: " + PerfMonitor.fps.toFixed(1)
                color: "#aaaaaa"
                font.pixelSize: 12
                visible: PerfMonitor.fps > 0
            }
            Button {
                text: root.fullscreen ? "Exit Fullscreen" : "Fullscreen"
                onClicked: root.fullscreen = !root.fullscreen
            }
        }
    }

    function takeScreenshot() {
        var now = new Date();
        var ts = now.getFullYear() + "-" +
                 String(now.getMonth()+1).padStart(2, '0') + "-" +
                 String(now.getDate()).padStart(2, '0') + "_" +
                 String(now.getHours()).padStart(2, '0') + "-" +
                 String(now.getMinutes()).padStart(2, '0') + "-" +
                 String(now.getSeconds()).padStart(2, '0');
        var path = "screenshots/chimera_" + ts + ".png";
        if (guestDisplay.saveScreenshot(path)) {
            console.log("Screenshot saved to " + path);
        } else {
            console.log("Screenshot failed");
        }
    }

    // Side panel (collapsible)
    Rectangle {
        id: sidePanel
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 240
        color: "#252525"
        visible: false

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Label { text: "Installed Apps"; color: "white"; font.bold: true }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ["Settings", "Chrome", "Files"]
                delegate: Button {
                    text: modelData
                    Layout.fillWidth: true
                }
            }
        }
    }

    // Keyboard shortcuts
    Shortcut {
        sequence: "F11"
        onActivated: root.fullscreen = !root.fullscreen
    }
    Shortcut {
        sequence: "Ctrl+Shift+A"
        onActivated: inputOverlay.visible = !inputOverlay.visible
    }
    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: root.takeScreenshot()
    }

    // Multi-Instance Dialog
    Dialog {
        id: multiInstanceDialog
        title: "Multi-Instance Manager"
        modal: true
        anchors.centerIn: parent
        width: 560
        height: 450

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            ListView {
                id: instanceList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: InstanceManager.listInstances()
                clip: true
                delegate: Rectangle {
                    width: instanceList.width
                    height: 44
                    color: index % 2 === 0 ? "#2d2d2d" : "#252525"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6
                        Label {
                            text: modelData
                            color: "white"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "Start"
                            flat: true
                            onClicked: InstanceManager.startInstance(modelData)
                        }
                        Button {
                            text: "Stop"
                            flat: true
                            onClicked: InstanceManager.stopInstance(modelData)
                        }
                        Button {
                            text: "Clone"
                            flat: true
                            onClicked: cloneDialog.openWithSource(modelData)
                        }
                        Button {
                            text: "Delete"
                            flat: true
                            onClicked: {
                                InstanceManager.deleteInstance(modelData);
                                instanceList.model = InstanceManager.listInstances();
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: newInstanceName
                    placeholderText: "New instance name"
                    Layout.fillWidth: true
                }
                Button {
                    text: "Create"
                    onClicked: {
                        if (newInstanceName.text.length > 0) {
                            InstanceManager.createInstance(newInstanceName.text, 4, 4096, 1920, 1080);
                            newInstanceName.text = "";
                            instanceList.model = InstanceManager.listInstances();
                        }
                    }
                }
                Button {
                    text: "Refresh"
                    onClicked: instanceList.model = InstanceManager.listInstances()
                }
            }
        }
    }

    // Clone Instance Dialog
    Dialog {
        id: cloneDialog
        title: "Clone Instance"
        modal: true
        anchors.centerIn: parent
        width: 360
        height: 180

        property string sourceName: ""

        function openWithSource(name) {
            sourceName = name;
            cloneNameField.text = name + "_clone";
            open();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                text: "Clone from: " + cloneDialog.sourceName
                color: "white"
            }
            TextField {
                id: cloneNameField
                placeholderText: "New instance name"
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: "Clone"
                    onClicked: {
                        if (cloneNameField.text.length > 0) {
                            InstanceManager.cloneInstance(cloneDialog.sourceName, cloneNameField.text);
                            cloneDialog.close();
                            instanceList.model = InstanceManager.listInstances();
                        }
                    }
                }
                Button {
                    text: "Cancel"
                    onClicked: cloneDialog.close()
                }
            }
        }
    }

    // Macro Dialog
    Dialog {
        id: macroDialog
        title: "Macro Manager"
        modal: true
        anchors.centerIn: parent
        width: 480
        height: 420

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Button {
                    text: MacroEngine.recording ? "Stop Recording" : "Record"
                    highlighted: MacroEngine.recording
                    onClicked: {
                        if (MacroEngine.recording) {
                            MacroEngine.stopRecording();
                            macroList.model = MacroEngine.listMacros();
                        } else {
                            if (macroNameField.text.length > 0) {
                                MacroEngine.startRecording(macroNameField.text);
                            }
                        }
                    }
                }
                TextField {
                    id: macroNameField
                    placeholderText: "Macro name"
                    Layout.fillWidth: true
                    enabled: !MacroEngine.recording
                }
            }

            Label {
                text: "Saved Macros"
                color: "white"
                font.bold: true
            }

            ListView {
                id: macroList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: MacroEngine.listMacros()
                clip: true
                delegate: Rectangle {
                    width: macroList.width
                    height: 40
                    color: index % 2 === 0 ? "#2d2d2d" : "#252525"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6
                        Label {
                            text: modelData
                            color: "white"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "Play"
                            flat: true
                            enabled: !MacroEngine.recording
                            onClicked: MacroEngine.startPlayback(modelData, loopCountField.text || 1)
                        }
                        Button {
                            text: "Delete"
                            flat: true
                            enabled: !MacroEngine.recording
                            onClicked: {
                                MacroEngine.deleteMacro(modelData);
                                macroList.model = MacroEngine.listMacros();
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label { text: "Loop count:"; color: "white" }
                TextField {
                    id: loopCountField
                    text: "1"
                    validator: IntValidator { bottom: 1; top: 999 }
                    Layout.preferredWidth: 60
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Stop Playback"
                    visible: MacroEngine.playing
                    onClicked: MacroEngine.stopPlayback()
                }
                Button {
                    text: "Refresh"
                    onClicked: macroList.model = MacroEngine.listMacros()
                }
            }
        }
    }
}
