pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Chimera.UI 1.0

Item {
    id: overlay

    property bool editing: false

    Rectangle {
        anchors.fill: parent
        color: overlay.editing ? "#33000000" : "#1a000000"
        border.color: overlay.editing ? "#884fd1a1" : "#444fd1a1"
        border.width: 1
        Behavior on color { ColorAnimation { duration: 160 } }
        Behavior on border.color { ColorAnimation { duration: 160 } }
    }

    Repeater {
        model: InputMapper.mappings

        delegate: Rectangle {
            id: bubble
            required property var modelData
            required property int index

            readonly property bool isDpad: modelData.type === "dpad"
            readonly property string keyLabel: modelData.key || "?"
            readonly property string textLabel: modelData.guidance || modelData.type

            x: overlay.width  * (modelData.x / 100.0) - width  / 2
            y: overlay.height * (modelData.y / 100.0) - height / 2
            width:  isDpad ? 120 : 54
            height: isDpad ? 120 : 54
            radius: isDpad ? 16 : width / 2
            border.color: "#eef4f1"
            border.width: 1.5
            scale: dragArea.pressed ? 1.08 : 1.0

            Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
            Behavior on color  { ColorAnimation  { duration: 160 } }

            gradient: Gradient {
                GradientStop { position: 0.0; color: overlay.editing ? "#e83ddc97" : "#bb3ddc97" }
                GradientStop { position: 1.0; color: overlay.editing ? "#d22fb87f" : "#a82fb87f" }
            }

            Column {
                anchors.centerIn: parent
                spacing: 2

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: bubble.keyLabel
                    color: "#06100d"
                    font.pixelSize: bubble.isDpad ? 18 : 15
                    font.weight: Font.Black
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: bubble.textLabel
                    color: "#10211b"
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    visible: text.length > 0
                    elide: Text.ElideRight
                    width: bubble.width - 8
                    horizontalAlignment: Text.AlignHCenter
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
                drag.maximumX: overlay.width  - parent.width
                drag.maximumY: overlay.height - parent.height
                onReleased: {
                    if (overlay.editing) {
                        const cx = (bubble.x + bubble.width  / 2) / overlay.width  * 100.0
                        const cy = (bubble.y + bubble.height / 2) / overlay.height * 100.0
                        InputMapper.updateMappingPosition(bubble.index, cx, cy)
                    }
                }
            }
        }
    }

    // Empty state hint
    Label {
        anchors.centerIn: parent
        visible: InputMapper.mappings.length === 0
        text: qsTr("尚無鍵位\n請在右側面板載入配置方案")
        color: "#ccffffff"
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: 13
        lineHeight: 1.5
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
