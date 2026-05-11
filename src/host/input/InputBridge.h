#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <filesystem>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace chimera::input {

/**
 * @brief Translates Host input (keyboard, mouse, gamepad) to Android guest input events.
 *
 * Phase 1 MVP: Uses ADB shell input for event injection.
 * Future: Replace with virtio-input for lower latency.
 */
class InputBridge {
public:
    struct Event {
        enum Type { KeyDown, KeyUp, MouseMove, MouseButtonDown, MouseButtonUp, Wheel, GamepadAxis, GamepadButton };
        Type type;
        int code;          // Key scan code or mouse button
        int value;         // For axes: -32768..32767; for buttons: 0/1
        int x, y;          // For mouse: absolute screen coordinates
        int relX, relY;    // For mouse: relative delta
        float pressure;    // For touch simulation
    };

    using EventCallback = std::function<void(const Event &)>;

    static InputBridge &instance();

    void setEventCallback(EventCallback cb);

    // ADB configuration for Phase 1 MVP
    void setAdbConfig(const std::filesystem::path &adbPath, int adbPort);

    // QMP low-latency input (preferred over ADB)
    void setQmpInput(class QmpInput *qmp);
    bool hasQmp() const;

    // Host-facing methods (called from Qt event handlers)
    void onKeyEvent(bool press, int nativeScanCode, int nativeVirtualKey);
    void onMouseMove(int x, int y, int dx, int dy);
    void onMouseButton(bool press, int button, int x, int y);
    void onWheel(int deltaX, int deltaY);
    void onGamepadButton(int deviceId, int button, bool pressed);
    void onGamepadAxis(int deviceId, int axis, float value);

    // Guest-facing methods
    void injectEvent(const Event &ev);

    // Enable/disable event forwarding
    void setForwardingEnabled(bool enabled);

    // Shutdown worker thread
    void shutdown();

private:
    InputBridge();
    ~InputBridge();
    void workerLoop();
    void enqueueAdbCommand(const std::string &cmd);
    int mapQtKeyToAndroid(int qtKey);

    EventCallback m_callback;
    bool m_forwarding = true;

    // ADB config
    std::filesystem::path m_adbPath;
    int m_adbPort = 5555;

    // QMP low-latency input
    class QmpInput *m_qmpInput = nullptr;

    // Command queue for async ADB execution
    std::queue<std::string> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running = true;
};

} // namespace chimera::input
