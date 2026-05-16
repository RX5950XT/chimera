#include "GamepadManager.h"
#include <windows.h>
#include <xinput.h>
#include <thread>
#include <cmath>

namespace chimera::input {

#pragma comment(lib, "xinput.lib")

GamepadManager &GamepadManager::instance() {
    static GamepadManager inst;
    return inst;
}

void GamepadManager::initialize() {
    m_initialized = true;
}

void GamepadManager::shutdown() {
    m_initialized = false;
}

void GamepadManager::poll() {
    if (!m_initialized) return;
    ++m_pollTick;
    for (int i = 0; i < 4; ++i) {
        // XInputGetState on an unplugged slot is comparatively slow. Probe
        // disconnected slots only ~2x/sec (staggered per slot); connected
        // controllers keep polling every frame for full responsiveness.
        if (!m_slotConnected[i] && (m_pollTick % 120u) != static_cast<unsigned>(i)) {
            continue;
        }
        XINPUT_STATE state;
        if (XInputGetState(i, &state) == ERROR_SUCCESS) {
            m_slotConnected[i] = true;
            GamepadState gs;
            gs.deviceId = i;
            gs.connected = true;
            gs.leftStickX = state.Gamepad.sThumbLX / 32767.0f;
            gs.leftStickY = state.Gamepad.sThumbLY / 32767.0f;
            gs.rightStickX = state.Gamepad.sThumbRX / 32767.0f;
            gs.rightStickY = state.Gamepad.sThumbRY / 32767.0f;
            gs.leftTrigger = state.Gamepad.bLeftTrigger / 255.0f;
            gs.rightTrigger = state.Gamepad.bRightTrigger / 255.0f;
            gs.buttons.resize(16);
            auto mapBtn = [&](WORD mask, size_t idx) { gs.buttons[idx] = (state.Gamepad.wButtons & mask) != 0; };
            mapBtn(XINPUT_GAMEPAD_A, 0);
            mapBtn(XINPUT_GAMEPAD_B, 1);
            mapBtn(XINPUT_GAMEPAD_X, 2);
            mapBtn(XINPUT_GAMEPAD_Y, 3);
            mapBtn(XINPUT_GAMEPAD_DPAD_UP, 4);
            mapBtn(XINPUT_GAMEPAD_DPAD_DOWN, 5);
            mapBtn(XINPUT_GAMEPAD_DPAD_LEFT, 6);
            mapBtn(XINPUT_GAMEPAD_DPAD_RIGHT, 7);
            mapBtn(XINPUT_GAMEPAD_LEFT_SHOULDER, 8);
            mapBtn(XINPUT_GAMEPAD_RIGHT_SHOULDER, 9);
            mapBtn(XINPUT_GAMEPAD_LEFT_THUMB, 10);
            mapBtn(XINPUT_GAMEPAD_RIGHT_THUMB, 11);
            mapBtn(XINPUT_GAMEPAD_START, 12);
            mapBtn(XINPUT_GAMEPAD_BACK, 13);
            if (m_callback) m_callback(i, gs);
            detectChanges(i, gs);
            m_prevState[i] = gs;
            m_hasPrevState[i] = true;
        } else {
            m_slotConnected[i] = false;
            m_hasPrevState[i] = false;
        }
    }
}

void GamepadManager::detectChanges(int deviceId, const GamepadState &current) {
    if (!m_hasPrevState[deviceId]) return;
    const auto &prev = m_prevState[deviceId];

    // Button transitions
    if (m_buttonCallback) {
        for (size_t i = 0; i < current.buttons.size() && i < prev.buttons.size(); ++i) {
            if (current.buttons[i] != prev.buttons[i]) {
                m_buttonCallback(deviceId, static_cast<int>(i), current.buttons[i]);
            }
        }
    }

    // Axis changes
    if (m_axisCallback) {
        auto checkAxis = [&](int axisId, float prevVal, float currVal) {
            constexpr float threshold = 0.15f;
            bool wasActive = std::abs(prevVal) > threshold;
            bool isActive = std::abs(currVal) > threshold;
            if (wasActive != isActive || (isActive && std::abs(currVal - prevVal) > 0.1f)) {
                m_axisCallback(deviceId, axisId, currVal);
            }
        };
        checkAxis(0, prev.leftStickX, current.leftStickX);
        checkAxis(1, prev.leftStickY, current.leftStickY);
        checkAxis(2, prev.rightStickX, current.rightStickX);
        checkAxis(3, prev.rightStickY, current.rightStickY);
        checkAxis(4, prev.leftTrigger, current.leftTrigger);
        checkAxis(5, prev.rightTrigger, current.rightTrigger);
    }
}

bool GamepadManager::isConnected(int deviceId) const {
    XINPUT_STATE state;
    return XInputGetState(deviceId, &state) == ERROR_SUCCESS;
}

GamepadState GamepadManager::getState(int deviceId) const {
    GamepadState gs;
    gs.deviceId = deviceId;
    gs.connected = false;
    return gs;
}

void GamepadManager::setStateCallback(StateCallback cb) {
    m_callback = cb;
}

void GamepadManager::setButtonCallback(ButtonCallback cb) {
    m_buttonCallback = cb;
}

void GamepadManager::setAxisCallback(AxisCallback cb) {
    m_axisCallback = cb;
}

void GamepadManager::setVibration(int deviceId, float leftMotor, float rightMotor) {
    XINPUT_VIBRATION vib;
    vib.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535);
    vib.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535);
    XInputSetState(deviceId, &vib);
}

} // namespace chimera::input
