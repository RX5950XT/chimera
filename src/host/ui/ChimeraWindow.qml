pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import Qt.labs.settings 1.0
import Qt.labs.platform as LabsPlatform
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

    onVisibilityChanged: {
        // Eco mode: lower emulator priority when minimized, restore when visible
        AndroidControls.setEcoMode(visibility === Window.Minimized || visibility === Window.Hidden)
    }

    // Persist window geometry across sessions
    Settings {
        id: appSettings
        category: "MainWindow"
        property int windowX: -1
        property int windowY: -1
        property int windowWidth: 1360
        property int windowHeight: 820
    }

    Component.onCompleted: {
        if (appSettings.windowX >= 0) root.x = appSettings.windowX
        if (appSettings.windowY >= 0) root.y = appSettings.windowY
        root.width  = appSettings.windowWidth
        root.height = appSettings.windowHeight
    }
    onXChanged:      appSettings.windowX      = root.x
    onYChanged:      appSettings.windowY      = root.y
    onWidthChanged:  appSettings.windowWidth  = root.width
    onHeightChanged: appSettings.windowHeight = root.height

    // System tray icon — click to restore, right-click menu
    LabsPlatform.SystemTrayIcon {
        id: trayIcon
        visible: true
        tooltip: qsTr("Chimera 模擬器")
        onActivated: function(reason) {
            if (reason === LabsPlatform.SystemTrayIcon.DoubleClick
                    || reason === LabsPlatform.SystemTrayIcon.Trigger) {
                root.show()
                root.raise()
                root.requestActivate()
            }
        }
        menu: LabsPlatform.Menu {
            LabsPlatform.MenuItem {
                text: qsTr("顯示")
                onTriggered: { root.show(); root.raise(); root.requestActivate() }
            }
            LabsPlatform.MenuSeparator {}
            LabsPlatform.MenuItem {
                text: qsTr("結束 Chimera")
                onTriggered: Qt.quit()
            }
        }
    }

    Connections {
        target: AndroidControls
        function onNotificationRequested(title, message) {
            trayIcon.showMessage(title, message)
        }
    }

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
        var dir = AndroidControls.screenshotDir()
        var name = "chimera_" + timestamp() + ".png"
        var path = dir + "/" + name
        var ok = nativeDisplay.attached ? nativeDisplay.saveScreenshot(path)
                                        : guestDisplay.saveScreenshot(path)
        if (!ok) {
            lastActionStatus = qsTr("截圖失敗")
            return
        }
        lastActionStatus = qsTr("截圖已儲存：") + name
        trayIcon.showMessage(qsTr("Chimera"), qsTr("截圖已儲存：") + name)
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
                                        text: nativeDisplay.attached
                                            ? InstanceManager.instanceFullConfig("chimera_dev").maxFps.toString()
                                            : PerfMonitor.fps.toFixed(0)
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
                            text: qsTr("安裝 OBB")
                            detail: qsTr("→ /obb/<pkg>/")
                            onClicked: obbInstallDialog.open()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("推送檔案")
                            detail: qsTr("→ Downloads")
                            onClicked: fileShareDialog.open()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("拉取檔案")
                            detail: qsTr("Downloads ←")
                            onClicked: pullFileDialog.open()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("應用程式")
                            detail: qsTr("App 管理")
                            onClicked: root.openSidePage("apps")
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
                            text: qsTr("剪貼簿同步")
                            detail: qsTr("→ Android")
                            onClicked: AndroidControls.syncClipboardToGuest()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("GPS 位置")
                            detail: qsTr("座標模擬")
                            onClicked: root.openSidePage("gps")
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("感應器 / 電池")
                            detail: qsTr("陀螺儀 / 加速度")
                            onClicked: root.openSidePage("sensor")
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

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 7
                            SideButton {
                                Layout.fillWidth: true
                                text: overlayVisible ? qsTr("關閉疊層") : qsTr("開啟疊層")
                                detail: qsTr("Overlay")
                                enabled: !nativeDisplay.attached
                                highlighted: overlayVisible
                                onClicked: overlayVisible = !overlayVisible
                            }
                            SideButton {
                                Layout.fillWidth: true
                                text: qsTr("清除所有")
                                onClicked: {
                                    InputMapper.clearMappings()
                                    lastActionStatus = qsTr("已清除所有鍵位")
                                }
                            }
                        }

                        SectionLabel { text: qsTr("載入 / 儲存方案") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 7
                            ComboBox {
                                id: schemeCombo
                                Layout.fillWidth: true
                                model: InputMapper.listSchemes()
                                displayText: currentIndex >= 0 ? currentText : qsTr("選擇方案…")
                                font.pixelSize: 12
                                contentItem: Text {
                                    leftPadding: 12
                                    text: schemeCombo.displayText
                                    font: schemeCombo.font
                                    color: theme.text
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    radius: 9
                                    color: theme.panelSoft
                                    border.color: schemeCombo.down ? theme.accent : theme.line
                                    Behavior on border.color { ColorAnimation { duration: 120 } }
                                }
                                popup: Popup {
                                    y: schemeCombo.height
                                    width: schemeCombo.width
                                    padding: 4
                                    contentItem: ListView {
                                        implicitHeight: contentHeight
                                        model: schemeCombo.delegateModel
                                        clip: true
                                    }
                                    background: Rectangle { radius: 9; color: theme.panel; border.color: theme.line }
                                }
                            }
                            DockButton {
                                text: qsTr("載入")
                                enabled: schemeCombo.currentIndex >= 0
                                onClicked: {
                                    if (InputMapper.loadScheme(schemeCombo.currentText))
                                        lastActionStatus = qsTr("已載入：") + schemeCombo.currentText
                                }
                            }
                            DockButton {
                                text: qsTr("儲存")
                                enabled: schemeCombo.currentIndex >= 0
                                onClicked: {
                                    if (InputMapper.saveScheme(schemeCombo.currentText))
                                        lastActionStatus = qsTr("已儲存：") + schemeCombo.currentText
                                }
                            }
                        }

                        SectionLabel { text: qsTr("目前鍵位綁定") }

                        ListView {
                            id: mappingsList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: InputMapper.mappings

                            Label {
                                anchors.centerIn: parent
                                visible: mappingsList.count === 0
                                text: qsTr("尚無鍵位綁定\n請載入遊戲配置檔")
                                color: theme.muted
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 12
                                lineHeight: 1.4
                            }

                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: mappingsList.width
                                height: 44
                                radius: 10
                                color: theme.card
                                border.color: theme.lineSoft

                                RowLayout {
                                    anchors { fill: parent; leftMargin: 12; rightMargin: 8; topMargin: 6; bottomMargin: 6 }
                                    spacing: 8

                                    Rectangle {
                                        width: 32; height: 24; radius: 6
                                        color: theme.accent
                                        Label {
                                            anchors.centerIn: parent
                                            text: modelData.key || "?"
                                            color: "white"
                                            font.pixelSize: 11
                                            font.weight: Font.Bold
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.guidance.length > 0 ? modelData.guidance
                                            : qsTr("%1 @ (%2, %3)").arg(modelData.type)
                                                 .arg(modelData.x.toFixed(1))
                                                 .arg(modelData.y.toFixed(1))
                                        color: theme.text
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    DockButton {
                                        text: qsTr("刪除")
                                        onClicked: InputMapper.removeMapping(index)
                                    }
                                }
                            }
                        }
                    }

                    // ---- Apps page -------------------------------------------
                    ColumnLayout {
                        id: appsPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "apps"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        property var pkgs: []

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: qsTr("應用程式"); Layout.fillWidth: true }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        DockButton {
                            Layout.fillWidth: true
                            text: qsTr("重新整理應用程式清單")
                            highlighted: true
                            onClicked: {
                                appsPage.pkgs = AndroidControls.listInstalledPackages()
                                lastActionStatus = qsTr("已取得 %1 個應用程式").arg(appsPage.pkgs.length)
                            }
                        }

                        ListView {
                            id: appsList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: appsPage.pkgs

                            Label {
                                anchors.centerIn: parent
                                visible: appsList.count === 0
                                text: qsTr("按「重新整理」取得已安裝的應用程式清單")
                                color: theme.muted
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                width: appsList.width - 24
                            }

                            delegate: Rectangle {
                                required property string modelData
                                required property int index
                                width: appsList.width
                                height: 44
                                radius: 10
                                color: theme.card
                                border.color: theme.lineSoft

                                RowLayout {
                                    anchors { fill: parent; leftMargin: 12; rightMargin: 8; topMargin: 6; bottomMargin: 6 }
                                    spacing: 8

                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData
                                        color: theme.text
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    DockButton {
                                        text: qsTr("啟動")
                                        highlighted: true
                                        onClicked: AndroidControls.launchPackage(modelData)
                                    }
                                    DockButton {
                                        text: qsTr("停止")
                                        onClicked: AndroidControls.forceStopPackage(modelData)
                                    }
                                    DockButton {
                                        text: qsTr("清除")
                                        onClicked: AndroidControls.clearPackageData(modelData)
                                    }
                                    DockButton {
                                        text: qsTr("卸載")
                                        onClicked: {
                                            AndroidControls.uninstallPackage(modelData)
                                            appsPage.pkgs = appsPage.pkgs.filter((_, i) => i !== index)
                                        }
                                    }
                                }
                            }
                        }
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
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 7
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
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("名稱排序")
                                onClicked: {
                                    InstanceManager.sortByName()
                                    inlineInstanceList.model = InstanceManager.listInstances()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 7
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("全部啟動")
                                highlighted: true
                                onClicked: InstanceManager.batchStart(InstanceManager.listInstances())
                            }
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("全部停止")
                                onClicked: InstanceManager.batchStop(InstanceManager.listInstances())
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
                                        DockButton { Layout.fillWidth: true; text: qsTr("暫停"); onClicked: InstanceManager.pauseInstance(modelData) }
                                        DockButton { Layout.fillWidth: true; text: qsTr("繼續"); onClicked: InstanceManager.resumeInstance(modelData) }
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

                    // ---- Sensor / Battery page -------------------------------
                    ColumnLayout {
                        id: sensorPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "sensor"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: qsTr("感應器 / 電池"); Layout.fillWidth: true }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        SectionLabel { text: qsTr("加速度計（m/s²）") }
                        Label { text: qsTr("水平 / 豎直持機"); color: theme.muted; font.pixelSize: 11; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    { label: qsTr("水平"), x: 0,    y: 9.8,  z: 0 },
                                    { label: qsTr("直立"), x: 0,    y: 0,    z: 9.8 },
                                    { label: qsTr("左傾"), x: -9.8, y: 0,    z: 0 },
                                    { label: qsTr("右傾"), x: 9.8,  y: 0,    z: 0 }
                                ]
                                delegate: DockButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    onClicked: {
                                        AndroidControls.setSensor("acceleration", modelData.x, modelData.y, modelData.z)
                                        lastActionStatus = qsTr("加速度：") + modelData.label
                                    }
                                }
                            }
                        }

                        SectionLabel { text: qsTr("陀螺儀（rad/s）") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    { label: qsTr("歸零"),   x: 0,   y: 0,   z: 0 },
                                    { label: qsTr("左轉"),   x: 0,   y: 1.5, z: 0 },
                                    { label: qsTr("右轉"),   x: 0,   y: -1.5,z: 0 },
                                    { label: qsTr("向前"),   x: 1.5, y: 0,   z: 0 }
                                ]
                                delegate: DockButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    onClicked: {
                                        AndroidControls.setSensor("gyroscope", modelData.x, modelData.y, modelData.z)
                                        lastActionStatus = qsTr("陀螺儀：") + modelData.label
                                    }
                                }
                            }
                        }

                        SectionLabel { text: qsTr("電池") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: [
                                    { label: "100%", val: 100 },
                                    { label: "80%",  val: 80  },
                                    { label: "50%",  val: 50  },
                                    { label: "20%",  val: 20  }
                                ]
                                delegate: DockButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    onClicked: {
                                        AndroidControls.setBatteryLevel(modelData.val)
                                        lastActionStatus = qsTr("電池：") + modelData.label
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("充電中")
                                onClicked: {
                                    AndroidControls.setBatteryStatus("charging")
                                    lastActionStatus = qsTr("電池狀態：充電中")
                                }
                            }
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("放電")
                                onClicked: {
                                    AndroidControls.setBatteryStatus("discharging")
                                    lastActionStatus = qsTr("電池狀態：放電")
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }

                    // ---- GPS page --------------------------------------------
                    ColumnLayout {
                        id: gpsPage
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "gps"
                        opacity: visible ? 1 : 0
                        spacing: 10
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: qsTr("GPS 位置模擬"); Layout.fillWidth: true }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        Label {
                            text: qsTr("目前：%1, %2").arg(AndroidControls.gpsLatitude.toFixed(6)).arg(AndroidControls.gpsLongitude.toFixed(6))
                            color: theme.muted
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }

                        Label { text: qsTr("緯度"); color: theme.text; font.pixelSize: 12 }
                        TextField {
                            id: gpsLatField
                            Layout.fillWidth: true
                            placeholderText: "25.033964"
                            text: AndroidControls.gpsLatitude !== 0 ? AndroidControls.gpsLatitude.toFixed(6) : ""
                            color: theme.text
                            background: Rectangle {
                                radius: 6
                                color: theme.panelSoft
                                border.color: parent.activeFocus ? theme.accent : theme.line
                            }
                            font.pixelSize: 13
                        }

                        Label { text: qsTr("經度"); color: theme.text; font.pixelSize: 12 }
                        TextField {
                            id: gpsLonField
                            Layout.fillWidth: true
                            placeholderText: "121.564468"
                            text: AndroidControls.gpsLongitude !== 0 ? AndroidControls.gpsLongitude.toFixed(6) : ""
                            color: theme.text
                            background: Rectangle {
                                radius: 6
                                color: theme.panelSoft
                                border.color: parent.activeFocus ? theme.accent : theme.line
                            }
                            font.pixelSize: 13
                        }

                        DockButton {
                            Layout.fillWidth: true
                            text: qsTr("套用 GPS 位置")
                            highlighted: true
                            onClicked: {
                                const lat = parseFloat(gpsLatField.text) || 0
                                const lon = parseFloat(gpsLonField.text) || 0
                                AndroidControls.setGpsLocation(lat, lon, 0)
                                lastActionStatus = qsTr("GPS 已設定：%1, %2").arg(lat.toFixed(6)).arg(lon.toFixed(6))
                            }
                        }

                        // Common cities
                        SectionLabel { text: qsTr("常用地點") }
                        Repeater {
                            model: [
                                { name: qsTr("台北"), lat: 25.033964, lon: 121.564468 },
                                { name: qsTr("東京"), lat: 35.689487, lon: 139.691706 },
                                { name: qsTr("首爾"), lat: 37.566535, lon: 126.977969 },
                                { name: qsTr("上海"), lat: 31.224361, lon: 121.469170 }
                            ]
                            delegate: DockButton {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.name
                                onClicked: {
                                    gpsLatField.text = modelData.lat.toFixed(6)
                                    gpsLonField.text = modelData.lon.toFixed(6)
                                    AndroidControls.setGpsLocation(modelData.lat, modelData.lon, 0)
                                    lastActionStatus = qsTr("GPS：%1").arg(modelData.name)
                                }
                            }
                        }

                        // Route simulation
                        SectionLabel { text: qsTr("路線模擬"); Layout.fillWidth: true }

                        Label {
                            Layout.fillWidth: true
                            text: AndroidControls.isGpsSimulating()
                                ? qsTr("🟢 路線模擬進行中")
                                : qsTr("⚪ 路線模擬已停止")
                            color: AndroidControls.isGpsSimulating() ? theme.accent : theme.muted
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 7
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("台北→東京 路線")
                                highlighted: true
                                onClicked: {
                                    AndroidControls.startGpsRoute(
                                        [[25.033, 121.565], [25.5, 123.5], [26.5, 127.0],
                                         [30.0, 132.0], [33.0, 135.0], [35.676, 139.650]],
                                        800.0)
                                }
                            }
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("停止路線")
                                onClicked: AndroidControls.stopGpsRoute()
                            }
                        }

                        Item { Layout.fillHeight: true }
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

                        SectionLabel { text: qsTr("裝置偽裝") }

                        Repeater {
                            model: InstanceManager.availableDeviceProfiles()
                            delegate: DockButton {
                                required property string modelData
                                Layout.fillWidth: true
                                text: modelData.length > 0 ? modelData : qsTr("無偽裝（預設）")
                                highlighted: settingsPage.cfg.deviceProfile === modelData
                                onClicked: {
                                    if (InstanceManager.setDeviceProfile("chimera_dev", modelData)) {
                                        settingsPage.cfg = InstanceManager.instanceFullConfig("chimera_dev")
                                        lastActionStatus = modelData.length > 0
                                            ? qsTr("裝置：") + modelData + qsTr("（下次啟動生效）")
                                            : qsTr("已關閉裝置偽裝（下次啟動生效）")
                                    }
                                }
                            }
                        }

                        SectionLabel { text: qsTr("螢幕密度 (DPI)") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: [160, 240, 320, 480]
                                delegate: DockButton {
                                    required property int modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    onClicked: {
                                        AndroidControls.setScreenDensity(modelData)
                                        lastActionStatus = qsTr("DPI 已設為 ") + modelData
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("重置 DPI")
                                onClicked: {
                                    AndroidControls.resetScreenDensity()
                                    lastActionStatus = qsTr("螢幕密度已重置")
                                }
                            }
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("飛行模式 開")
                                onClicked: {
                                    AndroidControls.setAirplaneMode(true)
                                    lastActionStatus = qsTr("飛行模式已開啟")
                                }
                            }
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("飛行模式 關")
                                onClicked: {
                                    AndroidControls.setAirplaneMode(false)
                                    lastActionStatus = qsTr("飛行模式已關閉")
                                }
                            }
                        }

                        SectionLabel { text: qsTr("螢幕尺寸") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: [
                                    { label: "手機 9:16",  w: 720,  h: 1280 },
                                    { label: "手機 9:19",  w: 1080, h: 2280 },
                                    { label: "平板 4:3",   w: 1200, h: 900  },
                                    { label: "橫屏 16:9",  w: 1280, h: 720  }
                                ]
                                delegate: DockButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    onClicked: {
                                        AndroidControls.setScreenSize(modelData.w, modelData.h)
                                        lastActionStatus = qsTr("解析度 %1×%2").arg(modelData.w).arg(modelData.h)
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("重置尺寸")
                                onClicked: {
                                    AndroidControls.resetScreenSize()
                                    lastActionStatus = qsTr("解析度已重置")
                                }
                            }
                        }

                        SectionLabel { text: qsTr("亮度") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                text: qsTr("暗")
                                color: theme.muted
                                font.pixelSize: 11
                            }
                            Slider {
                                id: brightnessSlider
                                Layout.fillWidth: true
                                from: 0; to: 255; value: 128
                                stepSize: 1
                                onMoved: AndroidControls.setScreenBrightness(Math.round(value))
                            }
                            Label {
                                text: qsTr("亮")
                                color: theme.muted
                                font.pixelSize: 11
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
                                text: settingsPage.cfg.enableAudio ? qsTr("音效 開") : qsTr("音效 關")
                                highlighted: settingsPage.cfg.enableAudio === true
                                onClicked: {
                                    const cur = settingsPage.cfg.enableAudio === true
                                    if (InstanceManager.setEnableAudio("chimera_dev", !cur)) {
                                        settingsPage.cfg = InstanceManager.instanceFullConfig("chimera_dev")
                                        lastActionStatus = !cur ? qsTr("音效已啟用（下次啟動生效）") : qsTr("音效已停用（下次啟動生效）")
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

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
                            text: qsTr("CPU / RAM / Root / 音效 變更需重啟 Instance")
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

    FileDialog {
        id: fileShareDialog
        title: qsTr("選擇要推送到 Android 的檔案")
        nameFilters: [qsTr("所有檔案 (*)")]
        onAccepted: AndroidControls.pushFileToGuest(selectedFile.toString())
    }

    // OBB expansion file install
    Dialog {
        id: obbInstallDialog
        title: qsTr("安裝 OBB 擴充資料")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: obbFilePath.text = ""
        onAccepted: {
            if (obbFilePath.text.trim() !== "" && obbPackageName.text.trim() !== "")
                AndroidControls.installObb(obbFilePath.text.trim(), obbPackageName.text.trim())
        }
        Column {
            spacing: 8
            width: 340
            Label { text: qsTr("OBB 本機路徑（如 C:/Downloads/main.obb）："); color: theme.text; font.pixelSize: 12 }
            TextField {
                id: obbFilePath
                width: parent.width
                placeholderText: "C:/Users/xxx/Downloads/main.123456.com.example.obb"
                color: theme.text
                placeholderTextColor: theme.muted
                font.pixelSize: 12
                background: Rectangle { radius: 8; color: "#10161c"; border.color: theme.lineSoft }
            }
            Label { text: qsTr("目標 Package 名稱（如 com.example.game）："); color: theme.text; font.pixelSize: 12 }
            TextField {
                id: obbPackageName
                width: parent.width
                placeholderText: "com.example.game"
                color: theme.text
                placeholderTextColor: theme.muted
                font.pixelSize: 12
                background: Rectangle { radius: 8; color: "#10161c"; border.color: theme.lineSoft }
            }
        }
    }

    // Pull file from /sdcard/Download/ — user types guest filename
    Dialog {
        id: pullFileDialog
        title: qsTr("從 Android 拉取檔案")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (pullFileNameInput.text.trim() !== "")
                AndroidControls.pullFileFromGuest(pullFileNameInput.text.trim())
        }
        Column {
            spacing: 8
            Label { text: qsTr("/sdcard/Download/ 中的檔名：") }
            TextField {
                id: pullFileNameInput
                width: 320
                placeholderText: "e.g. screenshot.png"
            }
        }
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
