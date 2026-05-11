import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Item {
    id: overlay
    visible: true
    opacity: 0.5

    property bool editing: false

    // Mock data: control mappings
    ListModel {
        id: controlsModel
        ListElement { type: "tap"; x: 10; y: 80; key: "1"; label: "Skill 1" }
        ListElement { type: "tap"; x: 15; y: 80; key: "2"; label: "Skill 2" }
        ListElement { type: "tap"; x: 20; y: 80; key: "3"; label: "Skill 3" }
        ListElement { type: "dpad"; x: 12; y: 60; key: "WASD"; label: "Move" }
    }

    Repeater {
        model: controlsModel
        delegate: Rectangle {
            x: parent.width * (model.x / 100.0)
            y: parent.height * (model.y / 100.0)
            width: model.type === "dpad" ? 120 : 48
            height: model.type === "dpad" ? 120 : 48
            color: "#00bcd4"
            border.color: "white"
            border.width: 2
            radius: 8
            opacity: overlay.opacity

            Label {
                anchors.centerIn: parent
                text: model.key
                color: "white"
                font.bold: true
            }

            MouseArea {
                anchors.fill: parent
                enabled: overlay.editing
                drag.target: parent
                onReleased: {
                    // Update normalized position back to model
                    var nx = (parent.x / overlay.width) * 100.0;
                    var ny = (parent.y / overlay.height) * 100.0;
                    console.log("Moved " + model.label + " to " + nx.toFixed(2) + ", " + ny.toFixed(2));
                }
            }
        }
    }

    // Edit mode toggle button
    Button {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 16
        text: overlay.editing ? "Done" : "Edit Controls"
        onClicked: overlay.editing = !overlay.editing
    }
}
