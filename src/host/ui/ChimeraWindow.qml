pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import Chimera.UI 1.0

ApplicationWindow {
    id: root
    visible: true
    title: qsTr("Chimera 模擬器")
    width: 1360
    height: 820
    minimumWidth: 960
    minimumHeight: 620
    color: theme.bg

    property bool overlayVisible: false
    property string sidePage: "main"
    property string lastActionStatus: ""
    readonly property bool isFullscreen: visibility === Window.FullScreen
    readonly property bool guestReady: nativeDisplay.attached || guestDisplay.hasFrame
    readonly property bool isRecording: ScreenRecorder.recording || nativeDisplay.recording

    QtObject {
        id: theme
        readonly property color bg: "#0a0e12"
        readonly property color panel: "#141a21"
        readonly property color panelSoft: "#1c242d"
        readonly property color card: "#1a222b"
        readonly property color line: "#2a343f"
        readonly property color lineSoft: "#202832"
        readonly property color text: "#e8edf2"
        readonly property color muted: "#8a97a6"
        readonly property color accent: "#3ddc97"
        readonly property color accent2: "#5ba8ff"
        readonly property color warn: "#ffcc66"
        readonly property color danger: "#ff6b6b"
    }

    component DockButton: Button {
        id: dockControl
        hoverEnabled: true
        implicitHeight: 36
        leftPadding: 16
        rightPadding: 16
        font.pixelSize: 13
        font.weight: Font.DemiBold
        scale: down ? 0.97 : 1.0

        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

        contentItem: Text {
            text: dockControl.text
            color: dockControl.enabled ? theme.text : theme.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font: dockControl.font
        }

        background: Rectangle {
            radius: 9
            color: dockControl.down ? "#2c5a4a"
                 : (dockControl.hovered ? "#243038" : theme.panelSoft)
            border.color: dockControl.highlighted ? theme.accent
                        : (dockControl.hovered ? "#3a4a56" : theme.line)
            border.width: 1
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on border.color { ColorAnimation { duration: 120 } }
        }
    }

    component SideButton: Button {
        id: sideControl
        property string detail: ""
        hoverEnabled: true
        implicitHeight: 48
        leftPadding: 14
        rightPadding: 12
        font.pixelSize: 13
        font.weight: Font.DemiBold
        scale: down ? 0.98 : 1.0

        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

        contentItem: RowLayout {
            spacing: 8

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: 3
                height: 18
                radius: 1.5
                color: sideControl.highlighted ? "#0d2a1f" : theme.accent
                opacity: sideControl.highlighted ? 1 : (sideControl.hovered ? 0.9 : 0.0)
                Behavior on opacity { NumberAnimation { duration: 140 } }
            }

            Text {
                text: sideControl.text
                color: sideControl.highlighted ? "#06110d" : theme.text
                font: sideControl.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
            }

            Text {
                text: sideControl.detail
                color: sideControl.highlighted ? "#1d3b30" : theme.muted
                font.pixelSize: 10
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
                Layout.alignment: Qt.AlignVCenter
                visible: sideControl.detail.length > 0
            }
        }

        background: Rectangle {
            radius: 11
            color: sideControl.highlighted ? theme.accent
                 : (sideControl.down ? "#243038"
                 : (sideControl.hovered ? "#212a33" : "#10161c"))
            border.color: sideControl.highlighted ? "#7ff0c3"
                        : (sideControl.hovered ? "#36444f" : theme.lineSoft)
            border.width: 1
            Behavior on color { ColorAnimation { duration: 130 } }
            Behavior on border.color { ColorAnimation { duration: 130 } }
        }
    }

    component NavButton: Button {
        id: navControl
        property string detail: ""
        hoverEnabled: true
        implicitHeight: 58
        font.pixelSize: 13
        font.weight: Font.DemiBold
        scale: down ? 0.96 : 1.0

        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

        contentItem: ColumnLayout {
            spacing: 2

            Text {
                text: navControl.text
                color: navControl.hovered ? theme.accent : theme.text
                font: navControl.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            Text {
                text: navControl.detail
                color: theme.muted
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
                visible: navControl.detail.length > 0
            }
        }

        background: Rectangle {
            radius: 13
            color: navControl.down ? "#2c5a4a"
                 : (navControl.hovered ? "#212a33" : "#10161c")
            border.color: navControl.hovered ? theme.accent : theme.lineSoft
            border.width: 1
            Behavior on color { ColorAnimation { duration: 120 } }
            Behavior on border.color { ColorAnimation { duration: 120 } }
        }
    }

    component SectionLabel: Label {
        color: theme.text
        font.pixelSize: 13
        font.weight: Font.Bold
        font.letterSpacing: 0.6
    }

    function toggleFullscreen() {
        visibility = isFullscreen ? Window.Windowed : Window.FullScreen
    }

    function timestamp() {
        var now = new Date()
        return now.getFullYear() + "-"
            + String(now.getMonth() + 1).padStart(2, "0") + "-"
            + String(now.getDate()).padStart(2, "0") + "_"
            + String(now.getHours()).padStart(2, "0") + "-"
            + String(now.getMinutes()).padStart(2, "0") + "-"
            + String(now.getSeconds()).padStart(2, "0")
    }

    function takeScreenshot() {
        var path = "screenshots/chimera_" + timestamp() + ".png"
        var ok = nativeDisplay.attached ? nativeDisplay.saveScreenshot(path)
                                        : guestDisplay.saveScreenshot(path)
        if (!ok) {
            lastActionStatus = qsTr("截圖失敗")
            console.log("Screenshot failed")
            return
        }
        lastActionStatus = qsTr("截圖已儲存")
    }

    function toggleRecording() {
        var path = "recordings/chimera_" + timestamp() + ".mp4"
        if (nativeDisplay.attached) {
            if (nativeDisplay.recording) {
                nativeDisplay.stopRecording()
                lastActionStatus = qsTr("錄影已停止")
            } else if (nativeDisplay.startRecording(path, 60)) {
                lastActionStatus = qsTr("錄影已開始")
            } else {
                lastActionStatus = qsTr("錄影啟動失敗")
            }
            return
        }

        if (ScreenRecorder.recording) {
            ScreenRecorder.stopRecording()
            lastActionStatus = qsTr("錄影已停止")
        } else {
            ScreenRecorder.startRecording(path, 60)
            lastActionStatus = qsTr("錄影已開始")
        }
    }

    function runAndroidAction(actionName, ok) {
        if (!ok) {
            lastActionStatus = qsTr("Android 尚未就緒：") + actionName
            console.log("Android action failed: " + actionName)
            return
        }
        lastActionStatus = qsTr("已送出：") + actionName
    }

    function openSidePage(page) {
        sidePage = page
        overlayVisible = false
    }

    function openKeyConfig() {
        if (nativeDisplay.attached) {
            openSidePage("keymap")
        } else {
            overlayVisible = !overlayVisible
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0a0e12" }
            GradientStop { position: 0.55; color: "#0d141a" }
            GradientStop { position: 1.0; color: "#10181f" }
        }
    }

    ColumnLayout {
        id: shell
        anchors.fill: parent
        anchors.margins: isFullscreen ? 0 : 16
        spacing: isFullscreen ? 0 : 14
        opacity: 0

        Component.onCompleted: introAnim.start()
        NumberAnimation {
            id: introAnim
            target: shell
            property: "opacity"
            from: 0; to: 1
            duration: 320
            easing.type: Easing.OutCubic
        }

        // ---- Top bar ------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: isFullscreen ? 0 : 62
            visible: !isFullscreen
            radius: 16
            color: theme.panel
            border.color: theme.line

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                height: 2
                radius: 1
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: theme.accent }
                    GradientStop { position: 0.5; color: theme.accent2 }
                    GradientStop { position: 1.0; color: "#1c242d" }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 16
                spacing: 14

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    width: 34
                    height: 34
                    radius: 10
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: theme.accent }
                        GradientStop { position: 1.0; color: theme.accent2 }
                    }
                    Label {
                        anchors.centerIn: parent
                        text: "C"
                        color: "#06110d"
                        font.pixelSize: 19
                        font.weight: Font.Black
                    }
                }

                ColumnLayout {
                    spacing: 1

                    Label {
                        text: qsTr("CHIMERA")
                        color: theme.text
                        font.pixelSize: 17
                        font.weight: Font.Black
                        font.letterSpacing: 1.5
                    }
                    Label {
                        text: qsTr("Android 遊戲執行環境")
                        color: theme.muted
                        font.pixelSize: 10
                        font.letterSpacing: 0.4
                    }
                }

                Item { Layout.fillWidth: true }

                // Status pill
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitHeight: 30
                    implicitWidth: statusRow.implicitWidth + 24
                    radius: 15
                    color: theme.panelSoft
                    border.color: root.guestReady ? "#2f6b51" : "#5a4a2a"

                    RowLayout {
                        id: statusRow
                        anchors.centerIn: parent
                        spacing: 7

                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: root.guestReady ? theme.accent : theme.warn
                            SequentialAnimation on opacity {
                                running: true
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                            }
                        }
                        Label {
                            text: nativeDisplay.attached ? qsTr("Native · 已連線")
                                : (guestDisplay.hasFrame ? qsTr("Stream · 已連線")
                                : qsTr("等待畫面"))
                            color: theme.text
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitHeight: 30
                    implicitWidth: recRow.implicitWidth + 22
                    radius: 15
                    visible: root.isRecording
                    color: "#3a1f1f"
                    border.color: theme.danger

                    RowLayout {
                        id: recRow
                        anchors.centerIn: parent
                        spacing: 6
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: theme.danger
                            SequentialAnimation on opacity {
                                running: root.isRecording
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.25; duration: 600 }
                                NumberAnimation { to: 1.0; duration: 600 }
                            }
                        }
                        Label {
                            text: qsTr("錄影中")
                            color: theme.text
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }

                DockButton {
                    text: root.isFullscreen ? qsTr("離開全螢幕") : qsTr("全螢幕")
                    onClicked: root.toggleFullscreen()
                }
            }
        }

        // ---- Main row -----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: isFullscreen ? 0 : 14

            Item {
                id: displayStage
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    id: displayShell
                    readonly property real guestAspect: 16 / 9
                    width: Math.min(parent.width, parent.height * guestAspect)
                    height: Math.min(parent.height, width / guestAspect)
                    anchors.centerIn: parent
                    radius: isFullscreen ? 0 : 16
                    color: "#020504"
                    border.color: isFullscreen ? "transparent" : theme.line
                    border.width: 1

                    GuestDisplay {
                        id: guestDisplay
                        objectName: "guestDisplay"
                        anchors.fill: parent
                        focus: true
                        visible: !nativeDisplay.attached
                    }

                    NativeEmulatorView {
                        id: nativeDisplay
                        objectName: "nativeDisplay"
                        anchors.fill: parent
                        instanceName: "chimera_dev"
                        consolePort: 5554
                        nativeEmbeddingEnabled: true
                    }

                    InputMapperOverlay {
                        id: inputOverlay
                        anchors.fill: parent
                        visible: overlayVisible && !nativeDisplay.attached
                        opacity: 0.82
                    }

                    // Loading placeholder shown until Android is ready
                    Column {
                        anchors.centerIn: parent
                        spacing: 14
                        visible: !root.guestReady
                        opacity: visible ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 44; height: 44; radius: 12
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: theme.accent }
                                GradientStop { position: 1.0; color: theme.accent2 }
                            }
                            Label {
                                anchors.centerIn: parent
                                text: "C"
                                color: "#06110d"
                                font.pixelSize: 24
                                font.weight: Font.Black
                            }
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("等待 Android 啟動…")
                            color: theme.muted
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                }
            }

            // ---- Side panel ----------------------------------------------
            Rectangle {
                Layout.preferredWidth: root.width >= 1200 ? 250 : 220
                Layout.fillHeight: true
                visible: !isFullscreen
                radius: 16
                color: theme.panel
                border.color: theme.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    // Performance stat card
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 92
                        radius: 13
                        color: theme.card
                        border.color: theme.lineSoft

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            ColumnLayout {
                                spacing: 0
                                Layout.alignment: Qt.AlignVCenter

                                RowLayout {
                                    spacing: 5
                                    Label {
                                        text: nativeDisplay.attached ? "60" : PerfMonitor.fps.toFixed(0)
                                        color: theme.accent
                                        font.pixelSize: 34
                                        font.weight: Font.Black
                                    }
                                    Label {
                                        text: "FPS"
                                        color: theme.muted
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                        Layout.alignment: Qt.AlignBottom
                                        bottomPadding: 7
                                    }
                                }
                                Label {
                                    text: nativeDisplay.attached ? qsTr("Native 顯示路徑")
                                                                 : qsTr("串流顯示路徑")
                                    color: theme.muted
                                    font.pixelSize: 10
                                }
                            }

                            Item { Layout.fillWidth: true }

                            ColumnLayout {
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 4

                                ColumnLayout {
                                    spacing: 0
                                    Label {
                                        text: qsTr("掉幀")
                                        color: theme.muted
                                        font.pixelSize: 10
                                    }
                                    Label {
                                        text: PerfMonitor.droppedFrames + " / " + PerfMonitor.totalFrames
                                        color: theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }

                    // ---- Main page -------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "main"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        SectionLabel { text: qsTr("ANDROID 導航") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("返回")
                                detail: qsTr("Back")
                                onClicked: root.runAndroidAction("back", AndroidControls.back())
                            }
                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("首頁")
                                detail: qsTr("Home")
                                onClicked: root.runAndroidAction("home", AndroidControls.home())
                            }
                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("最近")
                                detail: qsTr("Recents")
                                onClicked: root.runAndroidAction("recents", AndroidControls.recents())
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            NavButton {
                                Layout.fillWidth: true
                                text: "▲"
                                detail: qsTr("音量+")
                                onClicked: AndroidControls.volumeUp()
                            }
                            NavButton {
                                Layout.fillWidth: true
                                text: "▼"
                                detail: qsTr("音量-")
                                onClicked: AndroidControls.volumeDown()
                            }
                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("選單")
                                detail: qsTr("Menu")
                                onClicked: root.runAndroidAction("menu", AndroidControls.menu())
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            Layout.preferredHeight: 1
                            color: theme.lineSoft
                        }

                        SectionLabel { text: qsTr("操作") }

                        SideButton {
                            Layout.fillWidth: true
                            text: overlayVisible ? qsTr("隱藏鍵位") : qsTr("鍵位配置")
                            detail: qsTr("Shift+Tab")
                            highlighted: overlayVisible
                            onClicked: root.openKeyConfig()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("截圖")
                            detail: qsTr("Ctrl+Shift+S")
                            onClicked: root.takeScreenshot()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: root.isRecording ? qsTr("停止錄影") : qsTr("錄影")
                            detail: qsTr("Ctrl+Shift+R")
                            highlighted: root.isRecording
                            onClicked: root.toggleRecording()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("安裝 APK")
                            detail: qsTr("Ctrl+Shift+I")
                            onClicked: apkFileDialog.open()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("多開管理")
                            detail: qsTr("Ctrl+Shift+8")
                            onClicked: root.openSidePage("multi")
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("巨集管理")
                            detail: qsTr("Ctrl+Shift+7")
                            highlighted: MacroEngine.recording || MacroEngine.playing
                            onClicked: root.openSidePage("macro")
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("設定")
                            detail: qsTr("Ctrl+Shift+,")
                            onClicked: root.openSidePage("settings")
                        }

                        Item { Layout.fillHeight: true }

                        Label {
                            Layout.fillWidth: true
                            visible: AndroidControls.installStatus.length > 0
                            text: AndroidControls.installStatus
                            color: AndroidControls.installStatus.indexOf(qsTr("失敗")) >= 0 ? theme.danger
                                 : (AndroidControls.installStatus.indexOf(qsTr("成功")) >= 0 ? theme.accent
                                 : theme.warn)
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: lastActionStatus.length > 0
                            text: lastActionStatus
                            color: lastActionStatus.indexOf(qsTr("尚未就緒")) >= 0 ? theme.warn : theme.accent
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            font.weight: Font.Medium
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("點一下畫面後，鍵鼠會直接送進 Android。F11 全螢幕，Esc 離開。")
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            lineHeight: 1.3
                        }
                    }

                    // ---- Keymap page -----------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "keymap"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel {
                                text: qsTr("鍵位配置")
                                Layout.fillWidth: true
                            }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: nativeDisplay.attached
                                  ? qsTr("Native 顯示不能疊 QML 圖層，這裡改用側欄管理。既有快捷鍵仍會送進 Android。")
                                  : qsTr("串流顯示可直接在畫面上編輯鍵位。")
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            lineHeight: 1.3
                        }

                        SideButton { Layout.fillWidth: true; text: qsTr("WASD 移動"); detail: qsTr("預設") }
                        SideButton { Layout.fillWidth: true; text: qsTr("1 / 2 / 3 技能"); detail: qsTr("預設") }
                        SideButton {
                            Layout.fillWidth: true
                            text: overlayVisible ? qsTr("關閉畫面疊層") : qsTr("開啟畫面疊層")
                            detail: nativeDisplay.attached ? qsTr("串流限定") : qsTr("Overlay")
                            enabled: !nativeDisplay.attached
                            highlighted: overlayVisible
                            onClicked: overlayVisible = !overlayVisible
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // ---- Multi-instance page ---------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "multi"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel {
                                text: qsTr("多開管理")
                                Layout.fillWidth: true
                            }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        TextField {
                            id: inlineInstanceName
                            Layout.fillWidth: true
                            placeholderText: qsTr("新 instance 名稱")
                            color: theme.text
                            placeholderTextColor: theme.muted
                            font.pixelSize: 12
                            leftPadding: 12
                            rightPadding: 12
                            background: Rectangle {
                                radius: 10
                                color: "#10161c"
                                border.color: inlineInstanceName.activeFocus ? theme.accent : theme.lineSoft
                                Behavior on border.color { ColorAnimation { duration: 120 } }
                            }
                        }
                        DockButton {
                            Layout.fillWidth: true
                            text: qsTr("建立 Instance")
                            highlighted: true
                            onClicked: {
                                if (inlineInstanceName.text.length > 0) {
                                    InstanceManager.createInstance(inlineInstanceName.text, 4, 2048, 1280, 720)
                                    inlineInstanceName.text = ""
                                    inlineInstanceList.model = InstanceManager.listInstances()
                                }
                            }
                        }

                        ListView {
                            id: inlineInstanceList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: InstanceManager.listInstances()
                            delegate: Rectangle {
                                required property string modelData
                                width: inlineInstanceList.width
                                height: 122
                                radius: 12
                                color: theme.card
                                border.color: theme.lineSoft

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 11
                                    spacing: 8
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: theme.text
                                        elide: Text.ElideRight
                                        font.weight: Font.Bold
                                        font.pixelSize: 13
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 7
                                        DockButton { Layout.fillWidth: true; text: qsTr("啟動"); highlighted: true; onClicked: InstanceManager.startInstance(modelData) }
                                        DockButton { Layout.fillWidth: true; text: qsTr("停止"); onClicked: InstanceManager.stopInstance(modelData) }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 7
                                        DockButton {
                                            Layout.fillWidth: true
                                            text: qsTr("複製")
                                            onClicked: {
                                                InstanceManager.cloneInstance(modelData, modelData + "_clone")
                                                inlineInstanceList.model = InstanceManager.listInstances()
                                            }
                                        }
                                        DockButton {
                                            Layout.fillWidth: true
                                            text: qsTr("刪除")
                                            onClicked: {
                                                InstanceManager.deleteInstance(modelData)
                                                inlineInstanceList.model = InstanceManager.listInstances()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- Macro page ------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "macro"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel {
                                text: qsTr("巨集管理")
                                Layout.fillWidth: true
                            }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        TextField {
                            id: inlineMacroName
                            Layout.fillWidth: true
                            placeholderText: qsTr("巨集名稱")
                            enabled: !MacroEngine.recording
                            color: theme.text
                            placeholderTextColor: theme.muted
                            font.pixelSize: 12
                            leftPadding: 12
                            rightPadding: 12
                            background: Rectangle {
                                radius: 10
                                color: "#10161c"
                                border.color: inlineMacroName.activeFocus ? theme.accent : theme.lineSoft
                                Behavior on border.color { ColorAnimation { duration: 120 } }
                            }
                        }
                        DockButton {
                            Layout.fillWidth: true
                            text: MacroEngine.recording ? qsTr("停止錄製") : qsTr("開始錄製")
                            highlighted: MacroEngine.recording
                            onClicked: {
                                if (MacroEngine.recording) {
                                    MacroEngine.stopRecording()
                                    inlineMacroList.model = MacroEngine.listMacros()
                                } else if (inlineMacroName.text.length > 0) {
                                    MacroEngine.startRecording(inlineMacroName.text)
                                }
                            }
                        }

                        ListView {
                            id: inlineMacroList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 8
                            model: MacroEngine.listMacros()
                            delegate: Rectangle {
                                required property string modelData
                                width: inlineMacroList.width
                                height: 86
                                radius: 12
                                color: theme.card
                                border.color: theme.lineSoft

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 11
                                    spacing: 8
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: theme.text
                                        elide: Text.ElideRight
                                        font.weight: Font.Bold
                                        font.pixelSize: 13
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 7
                                        DockButton { Layout.fillWidth: true; text: qsTr("播放"); highlighted: true; enabled: !MacroEngine.recording; onClicked: MacroEngine.startPlayback(modelData, 1) }
                                        DockButton {
                                            Layout.fillWidth: true
                                            text: qsTr("刪除")
                                            enabled: !MacroEngine.recording
                                            onClicked: {
                                                MacroEngine.deleteMacro(modelData)
                                                inlineMacroList.model = MacroEngine.listMacros()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        DockButton {
                            Layout.fillWidth: true
                            text: qsTr("停止播放")
                            visible: MacroEngine.playing
                            onClicked: MacroEngine.stopPlayback()
                        }
                    }

                    // ---- Settings page ---------------------------------------
                    ColumnLayout {
                        id: settingsPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "settings"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        property var cfg: visible ? InstanceManager.instanceFullConfig("chimera_dev") : ({})

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: qsTr("設定"); Layout.fillWidth: true }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        // Info card
                        Rectangle {
                            Layout.fillWidth: true
                            height: infoCol.implicitHeight + 22
                            radius: 12
                            color: theme.card
                            border.color: theme.lineSoft

                            ColumnLayout {
                                id: infoCol
                                anchors { left: parent.left; right: parent.right; top: parent.top }
                                anchors.margins: 11
                                spacing: 6

                                Label {
                                    text: settingsPage.cfg.width + " × " + settingsPage.cfg.height
                                          + " · " + settingsPage.cfg.dpi + " DPI"
                                    color: theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    text: settingsPage.cfg.cpus + " CPU · "
                                          + settingsPage.cfg.ramMB + " MB RAM"
                                    color: theme.muted
                                    font.pixelSize: 11
                                }
                                Label {
                                    text: qsTr("引擎：") + (settingsPage.cfg.graphicsEngine || "angle")
                                          + " / " + (settingsPage.cfg.graphicsRenderer || "host")
                                    color: theme.muted
                                    font.pixelSize: 11
                                }
                            }
                        }

                        SectionLabel { text: qsTr("FPS 上限") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: [30, 60, 90, 120]
                                delegate: DockButton {
                                    required property int modelData
                                    Layout.fillWidth: true
                                    text: modelData + " FPS"
                                    highlighted: settingsPage.cfg.maxFps === modelData
                                    onClicked: {
                                        if (InstanceManager.updateInstanceFps("chimera_dev", modelData)) {
                                            settingsPage.cfg = InstanceManager.instanceFullConfig("chimera_dev")
                                            lastActionStatus = qsTr("FPS 已設為 ") + modelData + qsTr("（下次啟動生效）")
                                        }
                                    }
                                }
                            }
                        }

                        SectionLabel { text: qsTr("螢幕方向") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: [
                                    {label: qsTr("0°"),   deg: 0},
                                    {label: qsTr("90°"),  deg: 90},
                                    {label: qsTr("180°"), deg: 180},
                                    {label: qsTr("270°"), deg: 270}
                                ]
                                delegate: DockButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    onClicked: AndroidControls.setGuestRotation(modelData.deg)
                                }
                            }
                        }

                        SectionLabel { text: qsTr("進階") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("Eco 30FPS")
                                highlighted: settingsPage.cfg.maxFps === 30
                                onClicked: {
                                    if (InstanceManager.updateInstanceFps("chimera_dev", 30)) {
                                        settingsPage.cfg = InstanceManager.instanceFullConfig("chimera_dev")
                                        lastActionStatus = qsTr("Eco 模式已啟用（30 FPS）")
                                    }
                                }
                            }
                            DockButton {
                                Layout.fillWidth: true
                                text: settingsPage.cfg.enableRoot ? qsTr("Root 開") : qsTr("Root 關")
                                highlighted: settingsPage.cfg.enableRoot === true
                                onClicked: {
                                    const cur = settingsPage.cfg.enableRoot === true
                                    if (InstanceManager.setEnableRoot("chimera_dev", !cur)) {
                                        settingsPage.cfg = InstanceManager.instanceFullConfig("chimera_dev")
                                        lastActionStatus = qsTr("Root ") + (!cur ? qsTr("已啟用") : qsTr("已停用")) + qsTr("（下次啟動生效）")
                                    }
                                }
                            }
                        }

                        DockButton {
                            Layout.fillWidth: true
                            visible: settingsPage.cfg.enableRoot === true
                            text: qsTr("立即 adb root")
                            onClicked: AndroidControls.adbRoot()
                        }

                        Item { Layout.fillHeight: true }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("CPU / RAM / Root 變更需重啟 Instance")
                            color: theme.muted
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            lineHeight: 1.3
                        }
                    }
                }
            }
        }
    }

    // APK file picker
    FileDialog {
        id: apkFileDialog
        title: qsTr("選擇 APK 檔案")
        nameFilters: [qsTr("APK 檔案 (*.apk)"), qsTr("所有檔案 (*)")]
        onAccepted: AndroidControls.installApk(selectedFile.toString())
    }

    Shortcut {
        sequence: "F11"
        onActivated: root.toggleFullscreen()
    }
    Shortcut {
        sequence: "Esc"
        enabled: root.isFullscreen
        onActivated: root.visibility = Window.Windowed
    }
    Shortcut {
        sequence: "Ctrl+Shift+A"
        onActivated: root.openKeyConfig()
    }
    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: root.takeScreenshot()
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        onActivated: root.toggleRecording()
    }
    Shortcut {
        sequence: "Ctrl+Shift+7"
        onActivated: root.openSidePage("macro")
    }
    Shortcut {
        sequence: "Ctrl+Shift+I"
        onActivated: apkFileDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+Up"
        onActivated: AndroidControls.volumeUp()
    }
    Shortcut {
        sequence: "Ctrl+Down"
        onActivated: AndroidControls.volumeDown()
    }
    Shortcut {
        sequence: "Ctrl+Shift+,"
        onActivated: root.openSidePage("settings")
    }
    Shortcut {
        sequence: "Ctrl+Shift+8"
        onActivated: root.openSidePage("multi")
    }
    Shortcut {
        sequence: "Shift+Tab"
        onActivated: root.openKeyConfig()
    }
    Shortcut {
        sequence: "Alt+Left"
        onActivated: root.runAndroidAction("back", AndroidControls.back())
    }
    Shortcut {
        sequence: "Ctrl+Shift+H"
        onActivated: root.runAndroidAction("home", AndroidControls.home())
    }
    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        onActivated: root.runAndroidAction("recents", AndroidControls.recents())
    }
}
