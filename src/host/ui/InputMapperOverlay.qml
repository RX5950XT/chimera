pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: overlay

    property bool editing: false

    ListModel {
        id: controlsModel
        ListElement { type: "tap"; px: 74; py: 78; key: "1"; label: "技能 1" }
        ListElement { type: "tap"; px: 82; py: 78; key: "2"; label: "技能 2" }
        ListElement { type: "tap"; px: 90; py: 78; key: "3"; label: "技能 3" }
        ListElement { type: "dpad"; px: 12; py: 62; key: "WASD"; label: "移動" }
    }

    Rectangle {
        anchors.fill: parent
        color: overlay.editing ? "#33000000" : "#1a000000"
        border.color: overlay.editing ? "#884fd1a1" : "#444fd1a1"
        border.width: 1
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on border.color { ColorAnimation { duration: 160 } }
    }

    Repeater {
        model: controlsModel

        delegate: Rectangle {
            id: bubble
            required property string type
            required property real px
            required property real py
            required property string key
            required property string label

            x: parent.width * (px / 100.0)
            y: parent.height * (py / 100.0)
            width: type === "dpad" ? 124 : 56
            height: type === "dpad" ? 124 : 56
            radius: type === "dpad" ? 16 : width / 2
            border.color: "#eef4f1"
            border.width: 1.5
            scale: dragArea.pressed ? 1.08 : 1.0

            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 160 } }

            gradient: Gradient {
                GradientStop { position: 0.0; color: overlay.editing ? "#e83ddc97" : "#bb3ddc97" }
                GradientStop { position: 1.0; color: overlay.editing ? "#d22fb87f" : "#a82fb87f" }
            }

            Column {
                anchors.centerIn: parent
                spacing: 2

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: bubble.key
                    color: "#06100d"
                    font.pixelSize: bubble.type === "dpad" ? 18 : 16
                    font.weight: Font.Black
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: bubble.label
                    color: "#10211b"
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    visible: bubble.type === "dpad"
                }
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                enabled: overlay.editing
                cursorShape: overlay.editing ? Qt.SizeAllCursor : Qt.ArrowCursor
                drag.target: parent
                drag.minimumX: 0
                drag.minimumY: 0
                drag.maximumX: overlay.width - parent.width
                drag.maximumY: overlay.height - parent.height
            }
        }
    }

    Button {
        id: editButton
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 18
        hoverEnabled: true
        implicitHeight: 38
        leftPadding: 18
        rightPadding: 18
        text: overlay.editing ? qsTr("完成") : qsTr("編輯控制")
        font.pixelSize: 13
        font.weight: Font.DemiBold
        scale: down ? 0.96 : 1.0
        onClicked: overlay.editing = !overlay.editing

        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

        contentItem: Text {
            text: editButton.text
            color: overlay.editing ? "#06110d" : "#e8edf2"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: editButton.font
        }

        background: Rectangle {
            radius: 10
            color: overlay.editing ? "#3ddc97"
                 : (editButton.hovered ? "#243038" : "#1c242d")
            border.color: overlay.editing ? "#7ff0c3" : "#3a4a56"
            border.width: 1
            Behavior on color { ColorAnimation { duration: 130 } }
        }
    }
}
