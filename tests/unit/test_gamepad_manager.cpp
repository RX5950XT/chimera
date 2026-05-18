#include <QtTest>
#include "GamepadManager.h"

using namespace chimera::input;

class TestGamepadManager : public QObject {
    Q_OBJECT

private slots:
    void initializeAndShutdownDoNotCrash() {
        GamepadManager::instance().initialize();
        GamepadManager::instance().shutdown();
        QVERIFY(true);
    }

    void isConnectedReturnsFalseForInvalidDevice() {
        GamepadManager::instance().initialize();
        // No physical gamepads in test env — all 4 XInput slots should be disconnected
        for (int i = 0; i < 4; ++i)
            QVERIFY(!GamepadManager::instance().isConnected(i));
        GamepadManager::instance().shutdown();
    }

    void getStateReturnsDefaultForDisconnectedDevice() {
        GamepadManager::instance().initialize();
        const GamepadState st = GamepadManager::instance().getState(0);
        QVERIFY(!st.connected);
        GamepadManager::instance().shutdown();
    }

    void pollDoesNotCrash() {
        GamepadManager::instance().initialize();
        for (int i = 0; i < 5; ++i)
            GamepadManager::instance().poll();
        GamepadManager::instance().shutdown();
        QVERIFY(true);
    }

    void setCallbacksDoNotCrash() {
        GamepadManager::instance().initialize();
        GamepadManager::instance().setStateCallback(
            [](int, const GamepadState &) {});
        GamepadManager::instance().setButtonCallback(
            [](int, int, bool) {});
        GamepadManager::instance().setAxisCallback(
            [](int, int, float) {});
        GamepadManager::instance().poll();
        GamepadManager::instance().setStateCallback(nullptr);
        GamepadManager::instance().setButtonCallback(nullptr);
        GamepadManager::instance().setAxisCallback(nullptr);
        GamepadManager::instance().shutdown();
        QVERIFY(true);
    }

    void setVibrationOnDisconnectedDeviceDoesNotCrash() {
        GamepadManager::instance().initialize();
        GamepadManager::instance().setVibration(0, 0.5f, 0.5f);
        GamepadManager::instance().setVibration(3, 1.0f, 0.0f);
        GamepadManager::instance().shutdown();
        QVERIFY(true);
    }

    void multipleInitShutdownCyclesDoNotCrash() {
        for (int i = 0; i < 3; ++i) {
            GamepadManager::instance().initialize();
            GamepadManager::instance().poll();
            GamepadManager::instance().shutdown();
        }
        QVERIFY(true);
    }
};

QTEST_MAIN(TestGamepadManager)
#include "test_gamepad_manager.moc"
