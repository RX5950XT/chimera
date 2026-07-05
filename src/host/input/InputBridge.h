#pragma once

#include "CoordinateMapper.h"
#include <cstdint>
#include <string>
#include <functional>
#include <filesystem>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <array>

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

    // ADB configuration
    void setAdbConfig(const std::filesystem::path &adbPath, int adbPort,
                      const std::string &serial = {});

    // Android Console input (emulator.exe port 5554). Console is EXCLUDED
    // from all pointer chains: on the production emulator build the `event`
    // pointer commands return OK without injecting anything into the guest
    // (Session 108, getevent-proven) — a phantom channel that would trap
    // clicks ahead of the working ADB fallback. Keyboard keeps its own probe.
    void setConsoleInput(class AndroidConsoleInput *console);
    bool hasConsoleKeyboard() const;

    // Emulator gRPC keyboard input (port 8554) — the low-latency keyboard
    // path; the console has no working keyboard channel.
    void setGrpcInput(class EmulatorGrpcInput *grpc) { m_grpcInput = grpc; }
    // gRPC channel present AND its transport breaker closed. Gating every
    // gRPC branch on this (instead of bare non-null) is what makes the
    // console/QMP/ADB fallbacks reachable when the endpoint dies — a wired
    // m_grpcInput is otherwise permanently non-null, so input would vanish
    // silently ("picture but nothing responds").
    bool grpcUsable() const;

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
    void onWheel(int deltaX, int deltaY, int x = -1, int y = -1);
    void onGamepadButton(int deviceId, int button, bool pressed);
    void onGamepadAxis(int deviceId, int axis, float value);

    // Multi-touch: called per touch point on press, move, or release.
    void onTouchPoint(int pointId, int x, int y, bool pressed);

    // Unicode text input (from IME or clipboard paste).
    void onTextInput(const std::string &utf8text);

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
    std::string m_adbSerial;

    // Held mouse buttons bitmask (left=1, right=2, middle=4) — tracked for drag events
    int m_heldMouseButtons = 0;

    // Android Console input (emulator.exe port 5554)
    class AndroidConsoleInput *m_consoleInput = nullptr;
    class EmulatorGrpcInput *m_grpcInput = nullptr;
    // QMP low-latency input (QEMU backend)
    class QmpInput *m_qmpInput = nullptr;
    // HvSocket input (HCS/HyperV backend)
    class HvSocketTransport *m_hvSocketTransport = nullptr;
    int m_displayWidth = 1920;
    int m_displayHeight = 1080;
    std::chrono::steady_clock::time_point m_lastGrpcWheel{};
    std::chrono::steady_clock::time_point m_lastGrpcTouchMove{};
    CoordinateMapper m_mapper;

    // Command queue for async ADB execution
    std::queue<std::string> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_worker;
    bool m_running = true;
};

} // namespace chimera::input
