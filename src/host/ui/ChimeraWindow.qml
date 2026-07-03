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
    width: 1480
    height: 860
    minimumWidth: 960
    minimumHeight: 620
    color: theme.bg
    flags: Qt.Window | Qt.FramelessWindowHint

    property bool overlayVisible: false
    property bool perfHudVisible: false
    property string sidePage: "main"
    property int currentRotation: 0
    property string lastActionStatus: ""
    readonly property bool isFullscreen: visibility === Window.FullScreen
    // hasFrame alone is not "ready": with -no-boot-anim the capture path delivers
    // black frames during boot, so also wait for AndroidControls.bootReady (true by
    // default in modes without the emulator boot poller).
    readonly property bool guestReady: nativeDisplay.attached || (guestDisplay.hasFrame && AndroidControls.bootReady)
    readonly property bool isRecording: ScreenRecorder.recording || nativeDisplay.recording
    readonly property real effectiveFps: Math.max(
        0,
        Math.min(PerfMonitor.guestFps, PerfMonitor.renderFps, PerfMonitor.streamFps))

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
        property int windowWidth: 1480
        property int windowHeight: 860
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

    component TitleButton: Button {
        id: titleControl
        hoverEnabled: true
        implicitWidth: 42
        implicitHeight: 30
        font.pixelSize: 13
        font.weight: Font.DemiBold

        contentItem: Text {
            text: titleControl.text
            color: titleControl.hovered ? theme.text : theme.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: titleControl.font
        }

        background: Rectangle {
            radius: 7
            color: titleControl.down ? "#26323b"
                 : (titleControl.hovered ? "#202932" : "transparent")
            Behavior on color { ColorAnimation { duration: 100 } }
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

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: isFullscreen ? 0 : 38
        visible: !isFullscreen
        color: "#0c1117"
        border.color: theme.lineSoft
        z: 20

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: {
                root.visibility = root.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
            }
        }

        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active) root.startSystemMove()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            spacing: 9

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: 26
                height: 26
                radius: 7
                gradient: Gradient {
                    GradientStop { position: 0.0; color: theme.accent }
                    GradientStop { position: 1.0; color: theme.accent2 }
                }
                Label {
                    anchors.centerIn: parent
                    text: "C"
                    color: "#06110d"
                    font.pixelSize: 15
                    font.weight: Font.Black
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
                Label {
                    text: qsTr("CHIMERA")
                    color: theme.text
                    font.pixelSize: 16
                    font.weight: Font.Black
                    font.letterSpacing: 1.6
                }
            }

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                visible: root.isRecording
                implicitWidth: recTitleLabel.implicitWidth + 24
                implicitHeight: 24
                radius: 12
                color: "#321b1d"
                border.color: theme.danger
                Label {
                    id: recTitleLabel
                    anchors.centerIn: parent
                    text: qsTr("錄影中")
                    color: theme.text
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }

            Item { Layout.fillWidth: true }

            TitleButton {
                text: "-"
                onClicked: root.showMinimized()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("最小化")
            }
            TitleButton {
                text: root.visibility === Window.Maximized ? "□" : "□"
                onClicked: root.visibility = root.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
                ToolTip.visible: hovered
                ToolTip.text: root.visibility === Window.Maximized ? qsTr("還原") : qsTr("最大化")
            }
            TitleButton {
                id: closeTitleButton
                text: "X"
                hoverEnabled: true
                onClicked: Qt.quit()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("關閉")
                background: Rectangle {
                    radius: 7
                    color: closeTitleButton.down ? "#5a2327"
                         : (closeTitleButton.hovered ? "#8a2d35" : "transparent")
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
            }
        }
    }

    MouseArea {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 6
        visible: !isFullscreen
        z: 50
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
        width: 6
        visible: !isFullscreen
        z: 50
        cursorShape: Qt.SizeHorCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 6
        visible: !isFullscreen
        z: 50
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 6
        visible: !isFullscreen
        z: 50
        cursorShape: Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }

    ColumnLayout {
        id: shell
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: isFullscreen ? parent.top : titleBar.bottom
        anchors.margins: isFullscreen ? 0 : 10
        spacing: isFullscreen ? 0 : 10
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

        // ---- Main row -----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: isFullscreen ? 0 : 10

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
                    radius: isFullscreen ? 0 : 10
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
                        visible: nativeEmbedEnabled
                        enabled: nativeEmbedEnabled
                        nativeEmbeddingEnabled: nativeEmbedEnabled
                    }

                    InputMapperOverlay {
                        id: inputOverlay
                        anchors.fill: parent
                        visible: overlayVisible && !nativeDisplay.attached
                        opacity: 0.82
                    }

                    // Performance HUD overlay
                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: 10
                        visible: root.perfHudVisible && root.guestReady
                        width: hudCol.implicitWidth + 20
                        height: hudCol.implicitHeight + 16
                        radius: 10
                        color: "#e0050a0f"
                        border.color: "#334a5560"
                        border.width: 1
                        z: 10

                        Column {
                            id: hudCol
                            anchors.centerIn: parent
                            spacing: 3

                            Label {
                                text: "內容 " + PerfMonitor.guestFps.toFixed(0) + " FPS"
                                color: PerfMonitor.fps < 30 ? theme.danger
                                     : (PerfMonitor.fps < 50 ? theme.warn : theme.accent)
                                font.pixelSize: 16
                                font.weight: Font.Black
                                font.family: "Courier New"
                            }
                            Label {
                                visible: !nativeDisplay.attached
                                text: "串流 " + PerfMonitor.streamFps.toFixed(0)
                                      + "  顯示 " + PerfMonitor.renderFps.toFixed(0)
                                color: theme.muted
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                font.family: "Courier New"
                            }
                            Label {
                                visible: !nativeDisplay.attached && PerfMonitor.visibleLatencyMs >= 0
                                text: "延遲 " + PerfMonitor.visibleLatencyMs.toFixed(0) + " ms"
                                color: PerfMonitor.visibleLatencyMs > 50 ? theme.warn : theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                font.family: "Courier New"
                            }
                            Label {
                                text: "掉幀 " + PerfMonitor.droppedFrames
                                      + "  重複 " + (PerfMonitor.duplicateRate * 100).toFixed(0) + "%"
                                color: PerfMonitor.droppedFrames > 10 ? theme.warn : theme.muted
                                font.pixelSize: 12
                                font.weight: Font.Medium
                                font.family: "Courier New"
                            }
                        }
                    }

                    // Custom Chimera loading screen shown until Android is ready.
                    // Covers the display area while !guestReady so the guest's
                    // default (Pixel) boot animation is never seen. Wordmark +
                    // indeterminate progress bar instead of a center icon.
                    Column {
                        anchors.centerIn: parent
                        spacing: 20
                        width: Math.min(parent.width * 0.5, 320)
                        visible: !root.guestReady
                        opacity: visible ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 300 } }

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("CHIMERA")
                            color: theme.text
                            font.pixelSize: 26
                            font.weight: Font.Black
                            font.letterSpacing: 4
                        }

                        ProgressBar {
                            id: bootProgress
                            width: parent.width
                            indeterminate: true

                            background: Rectangle {
                                implicitHeight: 5
                                radius: 3
                                color: "#1c2630"
                            }
                            contentItem: Item {
                                implicitHeight: 5
                                clip: true
                                Rectangle {
                                    id: bootProgressPip
                                    width: bootProgress.width * 0.35
                                    height: parent.height
                                    radius: 3
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0.0; color: theme.accent }
                                        GradientStop { position: 1.0; color: theme.accent2 }
                                    }
                                    SequentialAnimation on x {
                                        running: bootProgress.visible
                                        loops: Animation.Infinite
                                        NumberAnimation { from: 0; to: bootProgress.width * 0.65; duration: 1100; easing.type: Easing.InOutQuad }
                                        NumberAnimation { from: bootProgress.width * 0.65; to: 0; duration: 1100; easing.type: Easing.InOutQuad }
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("正在啟動 Android…")
                            color: theme.muted
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                }
            }

            // ---- Side panel ----------------------------------------------
            Rectangle {
                Layout.preferredWidth: root.width >= 1200 ? 190 : 172
                Layout.fillHeight: true
                visible: !isFullscreen
                radius: 12
                color: theme.panel
                border.color: theme.line

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    // Compact FPS / fullscreen card
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: 10
                        color: theme.card
                        border.color: theme.lineSoft

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 6

                            Label {
                                text: root.effectiveFps.toFixed(0)
                                color: nativeDisplay.attached || root.effectiveFps >= 50
                                       ? theme.accent
                                       : (root.effectiveFps >= 30 ? theme.warn : theme.danger)
                                font.pixelSize: 26
                                font.weight: Font.Black
                                Layout.alignment: Qt.AlignVCenter
                            }

                            ColumnLayout {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.fillWidth: true
                                spacing: 0
                                Label {
                                    text: "FPS"
                                    color: theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Bold
                                }
                                Label {
                                    text: qsTr("有效")
                                    color: theme.muted
                                    font.pixelSize: 9
                                }
                            }

                            DockButton {
                                Layout.alignment: Qt.AlignVCenter
                                implicitWidth: 54
                                implicitHeight: 30
                                leftPadding: 8
                                rightPadding: 8
                                text: root.isFullscreen ? qsTr("退出") : qsTr("全螢幕")
                                font.pixelSize: 11
                                onClicked: root.toggleFullscreen()
                                ToolTip.visible: hovered
                                ToolTip.text: root.isFullscreen ? qsTr("離開全螢幕") : qsTr("進入全螢幕")
                            }
                        }
                    }

                    // ---- Main page -------------------------------------------
                    ScrollView {
                        id: mainPageScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "main"
                        opacity: visible ? 1 : 0
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                      ColumnLayout {
                        width: mainPageScroll.availableWidth
                        spacing: 10

                        // 常用應用程式：只有使用者釘選 app 時才顯示。
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: AndroidControls.pinnedApps.length > 0

                            SectionLabel { text: qsTr("常用應用程式") }

                            Repeater {
                                model: AndroidControls.pinnedApps
                                delegate: RowLayout {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    spacing: 6

                                    DockButton {
                                        Layout.fillWidth: true
                                        text: modelData.includes(".")
                                            ? modelData.substring(modelData.lastIndexOf(".") + 1)
                                            : modelData
                                        highlighted: true
                                        onClicked: {
                                            AndroidControls.launchPackage(modelData)
                                            lastActionStatus = qsTr("已啟動：") + modelData
                                        }
                                    }
                                    DockButton {
                                        text: "×"
                                        implicitWidth: 32
                                        onClicked: AndroidControls.unpinApp(modelData)
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.bottomMargin: 2
                                Layout.preferredHeight: 1
                                color: theme.lineSoft
                            }
                        }

                        SectionLabel { text: qsTr("Android 導航") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("返回")
                                detail: qsTr("上一頁")
                                onClicked: root.runAndroidAction("back", AndroidControls.back())
                            }
                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("首頁")
                                detail: qsTr("主畫面")
                                onClicked: root.runAndroidAction("home", AndroidControls.home())
                            }
                            NavButton {
                                Layout.fillWidth: true
                                text: qsTr("最近")
                                detail: qsTr("多工")
                                onClicked: root.runAndroidAction("recents", AndroidControls.recents())
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
                            text: guestDisplay.mouseLocked ? qsTr("解鎖鼠標") : qsTr("鎖定鼠標 (FPS)")
                            detail: qsTr("Alt+M")
                            highlighted: guestDisplay.mouseLocked
                            visible: !nativeDisplay.attached
                            onClicked: guestDisplay.setMouseLocked(!guestDisplay.mouseLocked)
                        }
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
                            text: qsTr("應用程式")
                            detail: qsTr("應用管理")
                            onClicked: root.openSidePage("apps")
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("剪貼簿同步")
                            detail: qsTr("送到 Android")
                            onClicked: AndroidControls.syncClipboardToGuest()
                        }
                        SideButton {
                            Layout.fillWidth: true
                            text: qsTr("設定")
                            detail: qsTr("Ctrl+Shift+,")
                            onClicked: root.openSidePage("settings")
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
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
                                detail: qsTr("覆蓋層")
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

                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: qsTr("應用程式"); Layout.fillWidth: true }
                            DockButton { text: qsTr("返回"); onClicked: root.openSidePage("main") }
                        }

                        TextField {
                            id: appsFilter
                            Layout.fillWidth: true
                            placeholderText: qsTr("搜尋應用程式…")
                            color: theme.text
                            placeholderTextColor: theme.muted
                            font.pixelSize: 12
                            leftPadding: 12; rightPadding: 12
                            background: Rectangle {
                                radius: 10; color: "#10161c"
                                border.color: appsFilter.activeFocus ? theme.accent : theme.lineSoft
                                Behavior on border.color { ColorAnimation { duration: 120 } }
                            }
                        }

                        DockButton {
                            Layout.fillWidth: true
                            text: qsTr("重新整理應用程式清單")
                            highlighted: true
                            onClicked: AndroidControls.refreshInstalledPackages()
                        }

                        ListView {
                            id: appsList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 6
                            model: AndroidControls.installedPackages.filter(
                                p => appsFilter.text.length === 0
                                  || p.toLowerCase().includes(appsFilter.text.toLowerCase()))

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
                                        text: AndroidControls.pinnedApps.includes(modelData)
                                            ? qsTr("已釘") : qsTr("釘選")
                                        highlighted: AndroidControls.pinnedApps.includes(modelData)
                                        onClicked: AndroidControls.pinnedApps.includes(modelData)
                                            ? AndroidControls.unpinApp(modelData)
                                            : AndroidControls.pinApp(modelData)
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
                                            AndroidControls.unpinApp(modelData)
                                            AndroidControls.uninstallPackage(modelData)
                                            Qt.callLater(() => AndroidControls.refreshInstalledPackages())
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
                            placeholderText: qsTr("新執行個體名稱")
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
                                text: qsTr("建立執行個體")
                                highlighted: true
                                onClicked: {
                                    if (inlineInstanceName.text.length > 0) {
                                        InstanceManager.createInstance(inlineInstanceName.text, 4, 2048, 1920, 1080)
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
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

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

                            RowLayout {
                                spacing: 4
                                Label { text: qsTr("循環"); color: theme.muted; font.pixelSize: 11 }
                                SpinBox {
                                    id: macroLoopCount
                                    from: 1; to: 999; value: 1
                                    implicitWidth: 72
                                    font.pixelSize: 12
                                    contentItem: TextInput {
                                        text: macroLoopCount.textFromValue(macroLoopCount.value)
                                        font: macroLoopCount.font
                                        color: theme.text
                                        horizontalAlignment: Qt.AlignHCenter
                                        verticalAlignment: Qt.AlignVCenter
                                        readOnly: !macroLoopCount.editable
                                        validator: macroLoopCount.validator
                                    }
                                    background: Rectangle {
                                        radius: 8
                                        color: theme.panelSoft
                                        border.color: theme.lineSoft
                                    }
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
                                        DockButton {
                                            Layout.fillWidth: true
                                            text: qsTr("播放")
                                            highlighted: true
                                            enabled: !MacroEngine.recording
                                            onClicked: MacroEngine.startPlayback(modelData, macroLoopCount.value)
                                        }
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
                    ScrollView {
                        id: sensorPageScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "sensor"
                        opacity: visible ? 1 : 0
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                      ColumnLayout {
                        id: sensorPage
                        width: sensorPageScroll.availableWidth
                        spacing: 10

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

                        SectionLabel { text: qsTr("手勢模擬") }
                        DockButton {
                            Layout.fillWidth: true
                            text: qsTr("震動裝置")
                            onClicked: AndroidControls.shakeDevice()
                        }
                      }
                    }

                    // ---- GPS page --------------------------------------------
                    ScrollView {
                        id: gpsPageScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "gps"
                        opacity: visible ? 1 : 0
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                      ColumnLayout {
                        id: gpsPage
                        width: gpsPageScroll.availableWidth
                        spacing: 10

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
                            text: AndroidControls.gpsSimulating
                                ? qsTr("路線模擬進行中")
                                : qsTr("路線模擬已停止")
                            color: AndroidControls.gpsSimulating ? theme.accent : theme.muted
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

                      }
                    }

                    // ---- Settings page ---------------------------------------
                    ScrollView {
                        id: settingsPageScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: sidePage === "settings"
                        opacity: visible ? 1 : 0
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

                      ColumnLayout {
                        id: settingsPage
                        width: settingsPageScroll.availableWidth
                        spacing: 10

                        property var cfg: settingsPageScroll.visible ? InstanceManager.instanceFullConfig("chimera_dev") : ({})

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
                                    highlighted: root.currentRotation === modelData.deg
                                    onClicked: {
                                        root.currentRotation = modelData.deg
                                        AndroidControls.setGuestRotation(modelData.deg)
                                    }
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
                                    { label: "橫屏 1080p", w: 1920, h: 1080 },
                                    { label: "橫屏 1440p", w: 2560, h: 1440 }
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

                        SectionLabel { text: qsTr("網路代理") }

                        Rectangle {
                            Layout.fillWidth: true
                            height: proxyCol.implicitHeight + 20
                            radius: 11
                            color: theme.card
                            border.color: AndroidControls.proxyEnabled ? "#2f6b51" : theme.lineSoft

                            ColumnLayout {
                                id: proxyCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                                spacing: 6

                                Label {
                                    text: AndroidControls.proxyEnabled
                                        ? qsTr("目前：%1:%2").arg(AndroidControls.proxyHost).arg(AndroidControls.proxyPort)
                                        : qsTr("未設定")
                                    color: AndroidControls.proxyEnabled ? theme.accent : theme.muted
                                    font.pixelSize: 11
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    TextField {
                                        id: proxyHostField
                                        Layout.fillWidth: true
                                        placeholderText: qsTr("主機 / IP")
                                        text: AndroidControls.proxyHost
                                        color: theme.text
                                        placeholderTextColor: theme.muted
                                        font.pixelSize: 12
                                        leftPadding: 8; rightPadding: 8
                                        background: Rectangle {
                                            radius: 7; color: "#10161c"
                                            border.color: parent.activeFocus ? theme.accent : theme.lineSoft
                                        }
                                    }
                                    TextField {
                                        id: proxyPortField
                                        implicitWidth: 70
                                        placeholderText: "8080"
                                        text: AndroidControls.proxyPort > 0 ? AndroidControls.proxyPort.toString() : ""
                                        color: theme.text
                                        placeholderTextColor: theme.muted
                                        font.pixelSize: 12
                                        leftPadding: 8; rightPadding: 8
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                        background: Rectangle {
                                            radius: 7; color: "#10161c"
                                            border.color: parent.activeFocus ? theme.accent : theme.lineSoft
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    DockButton {
                                        Layout.fillWidth: true
                                        text: qsTr("套用代理")
                                        highlighted: true
                                        onClicked: AndroidControls.setNetworkProxy(
                                            proxyHostField.text, parseInt(proxyPortField.text) || 0)
                                    }
                                    DockButton {
                                        Layout.fillWidth: true
                                        text: qsTr("清除代理")
                                        enabled: AndroidControls.proxyEnabled
                                        onClicked: AndroidControls.clearNetworkProxy()
                                    }
                                }
                            }
                        }

                        SectionLabel { text: qsTr("網速模擬（主控台）") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: ["full", "lte", "hsdpa", "umts", "edge", "gprs"]
                                delegate: DockButton {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData.toUpperCase()
                                    onClicked: {
                                        AndroidControls.setNetworkSpeed(modelData)
                                        lastActionStatus = qsTr("網速：") + modelData.toUpperCase()
                                    }
                                }
                            }
                        }

                        SectionLabel { text: qsTr("進階") }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("省電 30FPS")
                                highlighted: settingsPage.cfg.maxFps === 30
                                onClicked: {
                                    if (InstanceManager.updateInstanceFps("chimera_dev", 30)) {
                                        settingsPage.cfg = InstanceManager.instanceFullConfig("chimera_dev")
                                        lastActionStatus = qsTr("省電模式已啟用（30 FPS）")
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

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            DockButton {
                                Layout.fillWidth: true
                                text: qsTr("重新啟動 Android")
                                onClicked: {
                                    AndroidControls.rebootGuest()
                                    lastActionStatus = qsTr("Android 重啟中…")
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            text: qsTr("CPU / RAM / Root / 音效變更需重啟執行個體")
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

    // OBB expansion file install — pick file then enter package name
    FileDialog {
        id: obbFileDialog
        title: qsTr("選擇 OBB 檔案")
        nameFilters: [qsTr("OBB 擴充資料 (*.obb)"), qsTr("所有檔案 (*)")]
        onAccepted: {
            obbSelectedPath.text = selectedFile.toString()
            obbInstallDialog.open()
        }
    }

    Dialog {
        id: obbInstallDialog
        title: qsTr("安裝 OBB — 輸入套件名稱")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const pkg = obbPackageName.text.trim()
            if (pkg.length > 0)
                AndroidControls.installObb(obbSelectedPath.text, pkg)
        }
        Column {
            spacing: 8
            width: 340
            Label {
                text: qsTr("OBB 檔案：") + (obbSelectedPath.text.length > 0
                    ? obbSelectedPath.text.replace(/.*[/\\]/, "")
                    : qsTr("（未選擇）"))
                color: theme.muted; font.pixelSize: 11
                width: parent.width; wrapMode: Text.WordWrap
            }
            Label { text: qsTr("目標套件名稱（如 com.example.game）："); color: theme.text; font.pixelSize: 12 }
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
    // Invisible storage for the selected OBB path
    TextField { id: obbSelectedPath; visible: false }

    // Pull file from /sdcard/Download/ — shows file list + manual fallback
    Dialog {
        id: pullFileDialog
        title: qsTr("從 Android /sdcard/Download/ 拉取檔案")
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: AndroidControls.refreshGuestDownloads()
        onAccepted: {
            const name = pullFileNameInput.text.trim()
            if (name.length > 0)
                AndroidControls.pullFileFromGuest(name)
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                text: qsTr("/sdcard/Download/ 檔案列表")
                color: theme.muted
                font.pixelSize: 11
            }

            ListView {
                id: pullFileList
                Layout.fillWidth: true
                height: Math.min(contentHeight, 200)
                clip: true
                spacing: 4
                model: AndroidControls.guestDownloads

                Label {
                    anchors.centerIn: parent
                    visible: pullFileList.count === 0
                    text: qsTr("讀取中…")
                    color: theme.muted
                    font.pixelSize: 11
                }

                delegate: Rectangle {
                    required property string modelData
                    width: pullFileList.width
                    height: 34
                    radius: 8
                    color: pullFileNameInput.text === modelData ? theme.accent : theme.card
                    border.color: theme.lineSoft

                    Text {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 10 }
                        text: modelData
                        color: pullFileNameInput.text === modelData ? "#06110d" : theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: pullFileNameInput.text = modelData
                    }
                }
            }

            Label { text: qsTr("或手動輸入檔名："); color: theme.text; font.pixelSize: 11 }
            TextField {
                id: pullFileNameInput
                Layout.fillWidth: true
                placeholderText: "screenshot.png"
                color: theme.text
                placeholderTextColor: theme.muted
                font.pixelSize: 12
                background: Rectangle { radius: 8; color: "#10161c"; border.color: theme.lineSoft }
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
    Shortcut {
        sequence: "Ctrl+Shift+G"
        onActivated: root.openSidePage("gps")
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        onActivated: {
            AndroidControls.syncClipboardToGuest()
            lastActionStatus = qsTr("剪貼簿已同步")
        }
    }
    // Macro record start/stop
    Shortcut {
        sequence: "Ctrl+Shift+F5"
        onActivated: {
            if (MacroEngine.recording) {
                MacroEngine.stopRecording()
                lastActionStatus = qsTr("巨集錄製已停止")
            } else {
                root.openSidePage("macro")
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+F6"
        onActivated: MacroEngine.stopPlayback()
    }
    Shortcut {
        sequence: "Alt+M"
        enabled: !nativeDisplay.attached
        onActivated: guestDisplay.setMouseLocked(!guestDisplay.mouseLocked)
    }
    Shortcut {
        sequence: "Ctrl+Shift+P"
        onActivated: root.perfHudVisible = !root.perfHudVisible
    }
    // BlueStacks-parity shortcuts
    Shortcut {
        sequence: "Ctrl+Shift+3"
        onActivated: AndroidControls.shakeDevice()
    }
    Shortcut {
        sequence: "Ctrl+Shift+4"
        onActivated: {
            root.currentRotation = (root.currentRotation + 90) % 360
            AndroidControls.setGuestRotation(root.currentRotation)
            lastActionStatus = qsTr("旋轉至 ") + root.currentRotation + "°"
        }
    }
    Shortcut {
        // Boss key: hide window to system tray
        sequence: "Ctrl+Shift+X"
        onActivated: {
            root.hide()
            trayIcon.showMessage(qsTr("Chimera"), qsTr("Chimera 已縮至工作列，雙擊圖示可還原"))
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+T"
        onActivated: AndroidControls.trimMemory()
    }
    Shortcut {
        sequence: "Ctrl+Shift+M"
        onActivated: AndroidControls.toggleMute()
    }
    Shortcut {
        // Open host Downloads folder (where screenshots/recordings land)
        sequence: "Ctrl+Shift+6"
        onActivated: Qt.openUrlExternally(Qt.url("file:///" + AndroidControls.downloadDir().replace(/\\/g, "/")))
    }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        onActivated: {
            // Toggle eco mode manually (normally triggered by minimize; this is the BlueStacks parity shortcut)
            root.visibility = root.visibility === Window.Minimized
                ? Window.Windowed
                : Window.Minimized
        }
    }
}
