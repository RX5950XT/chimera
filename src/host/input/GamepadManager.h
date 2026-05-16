#pragma once

#include <string>
#include <vector>
#include <functional>

namespace chimera::input {

/**
 * @brief Manages XInput / DirectInput gamepads on Windows.
 */
struct GamepadState {
    int deviceId;
    bool connected;
    float leftStickX, leftStickY;
    float rightStickX, rightStickY;
    float leftTrigger, rightTrigger;
    std::vector<bool> buttons;  // Indexed by XInput button constant
};

class GamepadManager {
public:
    static GamepadManager &instance();

    void initialize();
    void shutdown();
    void poll();  // Call every frame

    bool isConnected(int deviceId) const;
    GamepadState getState(int deviceId) const;

    using StateCallback = std::function<void(int deviceId, const GamepadState &state)>;
    void setStateCallback(StateCallback cb);

    using ButtonCallback = std::function<void(int deviceId, int button, bool pressed)>;
    using AxisCallback = std::function<void(int deviceId, int axis, float value)>;
    void setButtonCallback(ButtonCallback cb);
    void setAxisCallback(AxisCallback cb);

    // Vibration
    void setVibration(int deviceId, float leftMotor, float rightMotor);

private:
    GamepadManager() = default;
    StateCallback m_callback;
    ButtonCallback m_buttonCallback;
    AxisCallback m_axisCallback;
    bool m_initialized = false;

    // Track previous state to detect transitions
    GamepadState m_prevState[4];
    bool m_hasPrevState[4] = {false, false, false, false};

    // Adaptive polling: empty XInput slots are only re-probed a few times per
    // second instead of every frame to avoid wasted CPU on unused controllers.
    bool m_slotConnected[4] = {false, false, false, false};
    unsigned m_pollTick = 0;

    void detectChanges(int deviceId, const GamepadState &current);
};

} // namespace chimera::input
