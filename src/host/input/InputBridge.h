#pragma once

#include "CoordinateMapper.h"
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

    // Android Console input (emulator.exe port 5554, preferred over QMP/ADB)
    void setConsoleInput(class AndroidConsoleInput *console);
    bool hasConsoleMouse() const;
    bool hasConsoleKeyboard() const;

    // QMP low-latency input (QEMU backend only — port 4444/4445)
    void setQmpInput(class QmpInput *qmp);
    bool hasQmp() const;

    // HvSocket input (highest priority — HCS/HyperV backend only)
    void setHvSocketTransport(class HvSocketTransport *hvs);
    bool hasHvSocket() const;

    void setDisplaySize(int width, int height);
    void setRotation(int degrees);

    CoordinateMapper &coordinateMapper() { return m_mapper; }

    // Host-facing methods (called from Qt event handlers)
    void onKeyEvent(bool press, int nativeScanCode, int nativeVirtualKey);
    void onMouseMove(int x, int y, int dx, int dy);
    void onMouseButton(bool press, int button, int x, int y);
    void onWheel(int deltaX, int deltaY);
    void onGamepadButton(int deviceId, int button, bool pressed);
    void onGamepadAxis(int deviceId, int axis, float value);

    // Android system actions such as Back/Home/Recents use Android keycodes
    // directly because QMP/Linux scan codes do not cover every semantic action.
    bool sendAndroidKeyCode(int androidKeyCode);

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
    void enqueueAdbCommand(const std::string &cmd, bool dropIfBacklogged = false);
    int mapQtKeyToAndroid(int qtKey);
    std::string keyNameFromQt(int qtKey) const;
    bool injectMappedKey(int qtKey);

    EventCallback m_callback;
    bool m_forwarding = true;

    // ADB config
    std::filesystem::path m_adbPath;
    int m_adbPort = 5555;

    // Held mouse buttons bitmask (left=1, right=2, middle=4) — tracked for drag events
    int m_heldMouseButtons = 0;

    // Android Console input (emulator.exe port 5554)
    class AndroidConsoleInput *m_consoleInput = nullptr;
    // QMP low-latency input (QEMU backend)
    class QmpInput *m_qmpInput = nullptr;
    // HvSocket input (HCS/HyperV backend)
    class HvSocketTransport *m_hvSocketTransport = nullptr;
    int m_displayWidth = 1920;
    int m_displayHeight = 1080;
    CoordinateMapper m_mapper;

    // Command queue for async ADB execution
    std::queue<std::string> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running = true;
};

} // namespace chimera::input
