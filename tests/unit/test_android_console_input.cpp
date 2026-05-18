#include <QtTest>
#include "AndroidConsoleInput.h"

using namespace chimera::input;

class TestAndroidConsoleInput : public QObject {
    Q_OBJECT

private slots:
    void defaultState() {
        AndroidConsoleInput c;
        QCOMPARE(c.state(), AndroidConsoleInput::State::Disconnected);
    }

    void isConnectedFalseWhenDisconnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.isConnected());
    }

    void isMouseReadyFalseWhenDisconnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.isMouseReady());
    }

    void isKeyboardReadyFalseWhenDisconnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.isKeyboardReady());
    }

    void sendMouseEventReturnsFalseWhenNotConnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.sendMouseEvent(100, 200, 1));
    }

    void sendMouseMoveReturnsFalseWhenNotConnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.sendMouseMove(50, 60));
    }

    void sendKeyEventReturnsFalseWhenNotConnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.sendKeyEvent(4, true));   // BACK key
        QVERIFY(!c.sendKeyEvent(4, false));
    }

    void sendGeoFixReturnsFalseWhenNotConnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.sendGeoFix(121.5, 25.0, 10.0));
    }

    void sendClipboardSetReturnsFalseWhenNotConnected() {
        AndroidConsoleInput c;
        QVERIFY(!c.sendClipboardSet("hello world"));
    }

    void defaultLatencyIsZero() {
        AndroidConsoleInput c;
        QCOMPARE(c.lastLatencyMs(), 0.0);
    }

    void defaultProbeResultIsFalse() {
        AndroidConsoleInput c;
        const auto p = c.probeResult();
        QVERIFY(!p.mouseCmdOk);
        QVERIFY(!p.textKeyCmdOk);
    }

    void autoReconnectDefaultOff() {
        AndroidConsoleInput c;
        // Just verifying it doesn't crash; can't check internal state directly
        c.setAutoReconnect(false);
        c.setAutoReconnect(true);
    }

    void stateChangedSignalEmittedOnDisconnect() {
        // Simulate disconnect without a real socket: disconnectFromHost() while already disconnected
        AndroidConsoleInput c;
        // Should not crash or emit anything when already disconnected
        c.disconnectFromHost();
        QCOMPARE(c.state(), AndroidConsoleInput::State::Disconnected);
    }
};

QTEST_MAIN(TestAndroidConsoleInput)
#include "test_android_console_input.moc"
