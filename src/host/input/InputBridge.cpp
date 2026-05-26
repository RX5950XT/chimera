#include "InputBridge.h"
#include "AndroidConsoleInput.h"
#include "EmulatorGrpcInput.h"
#include "InputMapper.h"
#include "QmpInput.h"
#include "HvSocketTransport.h"
#include <windows.h>
#include <QProcess>
#include <QDebug>
#include <Qt>
#include <algorithm>
#include <unordered_map>
#include <cmath>

namespace chimera::input {

// Qt keycode → Android keycode mapping (common keys)
static const std::unordered_map<int, int> s_keyMap = {
    {Qt::Key_Escape, 111},
    {Qt::Key_Tab, 61},
    {Qt::Key_Backspace, 67},
    {Qt::Key_Return, 66},
    {Qt::Key_Enter, 66},
    {Qt::Key_Shift, 59},
    {Qt::Key_Control, 113},
    {Qt::Key_Alt, 57},
    {Qt::Key_CapsLock, 115},
    {Qt::Key_NumLock, 143},
    {Qt::Key_ScrollLock, 116},
    {Qt::Key_Pause, 121},
    {Qt::Key_Space, 62},
    {Qt::Key_F1, 131}, {Qt::Key_F2, 132}, {Qt::Key_F3, 133}, {Qt::Key_F4, 134},
    {Qt::Key_F5, 135}, {Qt::Key_F6, 136}, {Qt::Key_F7, 137}, {Qt::Key_F8, 138},
    {Qt::Key_F9, 139}, {Qt::Key_F10, 140}, {Qt::Key_F11, 141}, {Qt::Key_F12, 142},
    {Qt::Key_Left, 21},
    {Qt::Key_Up, 19},
    {Qt::Key_Right, 22},
    {Qt::Key_Down, 20},
    {Qt::Key_Meta, 117},
    {Qt::Key_Home, 3},
    {Qt::Key_Back, 4},
    {Qt::Key_Menu, 82},
};

// Qt keycode → QEMU/Linux input keycode mapping (for QMP)
static const std::unordered_map<int, int> s_qemuKeyMap = {
    {Qt::Key_Escape, 1},
    {Qt::Key_1, 2}, {Qt::Key_2, 3}, {Qt::Key_3, 4}, {Qt::Key_4, 5},
    {Qt::Key_5, 6}, {Qt::Key_6, 7}, {Qt::Key_7, 8}, {Qt::Key_8, 9}, {Qt::Key_9, 10},
    {Qt::Key_0, 11},
    {Qt::Key_Minus, 12}, {Qt::Key_Equal, 13},
    {Qt::Key_Backspace, 14},
    {Qt::Key_Tab, 15},
    {Qt::Key_Q, 16}, {Qt::Key_W, 17}, {Qt::Key_E, 18}, {Qt::Key_R, 19},
    {Qt::Key_T, 20}, {Qt::Key_Y, 21}, {Qt::Key_U, 22}, {Qt::Key_I, 23},
    {Qt::Key_O, 24}, {Qt::Key_P, 25},
    {Qt::Key_BracketLeft, 26}, {Qt::Key_BracketRight, 27},
    {Qt::Key_Return, 28},
    {Qt::Key_Control, 29},
    {Qt::Key_A, 30}, {Qt::Key_S, 31}, {Qt::Key_D, 32}, {Qt::Key_F, 33},
    {Qt::Key_G, 34}, {Qt::Key_H, 35}, {Qt::Key_J, 36}, {Qt::Key_K, 37},
    {Qt::Key_L, 38},
    {Qt::Key_Semicolon, 39}, {Qt::Key_Apostrophe, 40}, {Qt::Key_Agrave, 41},
    {Qt::Key_Shift, 42},
    {Qt::Key_Backslash, 43},
    {Qt::Key_Z, 44}, {Qt::Key_X, 45}, {Qt::Key_C, 46}, {Qt::Key_V, 47},
    {Qt::Key_B, 48}, {Qt::Key_N, 49}, {Qt::Key_M, 50},
    {Qt::Key_Comma, 51}, {Qt::Key_Period, 52}, {Qt::Key_Slash, 53},
    {Qt::Key_Space, 57},
    {Qt::Key_F1, 59}, {Qt::Key_F2, 60}, {Qt::Key_F3, 61}, {Qt::Key_F4, 62},
    {Qt::Key_F5, 63}, {Qt::Key_F6, 64}, {Qt::Key_F7, 65}, {Qt::Key_F8, 66},
    {Qt::Key_F9, 67}, {Qt::Key_F10, 68}, {Qt::Key_F11, 87}, {Qt::Key_F12, 88},
    {Qt::Key_Left, 105}, {Qt::Key_Right, 106}, {Qt::Key_Up, 103}, {Qt::Key_Down, 108},
};

int qtMouseButtonToQmp(int button) {
    switch (button) {
    case Qt::RightButton:
        return 1;
    case Qt::MiddleButton:
        return 2;
    case Qt::LeftButton:
    default:
        return 0;
    }
}

constexpr int kPrimaryTouchId = 0;
constexpr int kWheelTouchId = 9;
constexpr int kTouchPressure = 1;

InputBridge &InputBridge::instance() {
    static InputBridge inst;
    return inst;
}

InputBridge::InputBridge() : m_worker([this]() { workerLoop(); }) {
    m_touchSlotIds.fill(-1);
}

InputBridge::~InputBridge() {
    shutdown();
}

void InputBridge::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

void InputBridge::setEventCallback(EventCallback cb) {
    m_callback = cb;
}

void InputBridge::setAdbConfig(const std::filesystem::path &adbPath, int adbPort,
                               const std::string &serial) {
    m_adbPath = adbPath;
    m_adbPort = adbPort;
    m_adbSerial = serial.empty()
        ? "emulator-" + std::to_string(adbPort - 1)
        : serial;
}

void InputBridge::setConsoleInput(AndroidConsoleInput *console) {
    m_consoleInput = console;
}

bool InputBridge::hasConsoleMouse() const {
    return m_consoleInput != nullptr && m_consoleInput->isMouseReady();
}

bool InputBridge::hasConsoleKeyboard() const {
    return m_consoleInput != nullptr && m_consoleInput->isKeyboardReady();
}

void InputBridge::setQmpInput(QmpInput *qmp) {
    m_qmpInput = qmp;
    if (m_qmpInput) {
        m_qmpInput->setDisplaySize(m_displayWidth, m_displayHeight);
    }
}

bool InputBridge::hasQmp() const {
    return m_qmpInput != nullptr && m_qmpInput->isConnected();
}

void InputBridge::setHvSocketTransport(HvSocketTransport *hvs) {
    m_hvSocketTransport = hvs;
}

bool InputBridge::hasHvSocket() const {
    return m_hvSocketTransport != nullptr && m_hvSocketTransport->isConnected();
}

void InputBridge::setDisplaySize(int width, int height) {
    if (width > 0) m_displayWidth = width;
    if (height > 0) m_displayHeight = height;
    m_mapper.setGuestSize(m_displayWidth, m_displayHeight);
    if (m_qmpInput) {
        m_qmpInput->setDisplaySize(m_displayWidth, m_displayHeight);
    }
}

void InputBridge::setRotation(int degrees) {
    m_mapper.setRotation(degrees);
}

void InputBridge::workerLoop() {
    while (true) {
        std::string cmd;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return !m_queue.empty() || !m_running; });
            if (!m_running && m_queue.empty()) break;
            cmd = std::move(m_queue.front());
            m_queue.pop();
        }
        if (m_adbPath.empty()) continue;
        // Execute ADB command synchronously in worker thread
        QStringList args;
        args << "-s" << QString::fromStdString(m_adbSerial);
        args << "shell" << QString::fromStdString(cmd);
        QProcess proc;
        proc.setProgram(QString::fromStdString(m_adbPath.string()));
        proc.setArguments(args);
        proc.start();
        if (!proc.waitForStarted(1000)) {
            qWarning() << "ADB command failed to start:" << QString::fromStdString(cmd);
            continue;
        }
        if (!proc.waitForFinished(2000)) {
            proc.kill();
            qWarning() << "ADB command timed out:" << QString::fromStdString(cmd);
            continue;
        }
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            qWarning() << "ADB command failed:" << QString::fromStdString(cmd)
                       << QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        }
    }
}

void InputBridge::enqueueAdbCommand(const std::string &cmd, bool dropIfBacklogged) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
        if (dropIfBacklogged && m_queue.size() > 8) return;
        while (m_queue.size() > 64) {
            m_queue.pop();
        }
        m_queue.push(cmd);
    }
    m_cv.notify_one();
}

int InputBridge::mapQtKeyToAndroid(int qtKey) {
    auto it = s_keyMap.find(qtKey);
    if (it != s_keyMap.end()) return it->second;
    // Fallback: try ASCII mapping for alphanumeric
    if (qtKey >= '0' && qtKey <= '9') return 7 + (qtKey - '0');
    if (qtKey >= 'A' && qtKey <= 'Z') return 29 + (qtKey - 'A');
    if (qtKey >= 'a' && qtKey <= 'z') return 29 + (qtKey - 'a');
    return -1;
}

int mapQtKeyToQemu(int qtKey) {
    auto it = s_qemuKeyMap.find(qtKey);
    if (it != s_qemuKeyMap.end()) return it->second;
    if (qtKey >= '0' && qtKey <= '9') return 2 + (qtKey - '0');
    if (qtKey >= 'A' && qtKey <= 'Z') return 30 + (qtKey - 'A');
    if (qtKey >= 'a' && qtKey <= 'z') return 30 + (qtKey - 'a');
    return -1;
}

std::string InputBridge::keyNameFromQt(int qtKey) const {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return std::string(1, static_cast<char>('A' + (qtKey - Qt::Key_A)));
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return std::string(1, static_cast<char>('0' + (qtKey - Qt::Key_0)));
    }
    switch (qtKey) {
    case Qt::Key_Space:
        return "Space";
    case Qt::Key_Tab:
        return "Tab";
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return "Enter";
    case Qt::Key_Escape:
        return "Esc";
    default:
        return {};
    }
}

bool InputBridge::injectMappedKey(int qtKey) {
    const std::string keyName = keyNameFromQt(qtKey);
    if (keyName.empty()) return false;

    const auto *mapping = InputMapper::instance().findMappingByKey(keyName);
    if (!mapping || mapping->type != "tap") return false;

    const int x = InputMapper::normToPixel(mapping->x, m_displayWidth);
    const int y = InputMapper::normToPixel(mapping->y, m_displayHeight);
    Event ev{};
    ev.type = Event::MouseButtonDown;
    ev.code = Qt::LeftButton;
    ev.x = x;
    ev.y = y;
    injectEvent(ev);

    Event release = ev;
    release.type = Event::MouseButtonUp;
    injectEvent(release);
    return true;
}

void InputBridge::onKeyEvent(bool press, int nativeScanCode, int nativeVirtualKey) {
    (void)nativeScanCode;
    if (!m_forwarding) return;
    if (press && injectMappedKey(nativeVirtualKey)) return;

    Event ev{};
    ev.type = press ? Event::KeyDown : Event::KeyUp;
    ev.code = nativeVirtualKey;
    injectEvent(ev);
}

void InputBridge::onMouseMove(int x, int y, int dx, int dy) {
    (void)dx; (void)dy;
    if (!m_forwarding) return;
    Event ev{};
    ev.type = Event::MouseMove;
    ev.x = x; ev.y = y;
    ev.code = m_heldMouseButtons;  // carry drag state (0 = hover, 1 = left drag, etc.)
    injectEvent(ev);
}

void InputBridge::onMouseButton(bool press, int button, int x, int y) {
    if (!m_forwarding) return;
    // Maintain held-button bitmask for drag events
    const int consoleBit = (button == Qt::LeftButton) ? 1 :
                           (button == Qt::RightButton) ? 2 : 4;
    if (press) m_heldMouseButtons |=  consoleBit;
    else       m_heldMouseButtons &= ~consoleBit;

    Event ev{};
    ev.type = press ? Event::MouseButtonDown : Event::MouseButtonUp;
    ev.code = button;
    ev.x = x; ev.y = y;
    injectEvent(ev);
}

void InputBridge::onWheel(int deltaX, int deltaY, int x, int y) {
    if (!m_forwarding) return;
    Event ev{};
    ev.type = Event::Wheel;
    ev.relX = deltaX; ev.relY = deltaY;
    ev.x = x;
    ev.y = y;
    injectEvent(ev);
}

void InputBridge::onTouchPoint(int pointId, int x, int y, bool pressed) {
    if (!m_forwarding) return;
    if (m_grpcInput) {
        m_grpcInput->sendTouch(pointId, x, y, pressed ? kTouchPressure : 0);
        return;
    }
    if (!m_consoleInput || !m_consoleInput->isMouseReady()) return;

    if (pressed) {
        int slot = -1;
        auto it = m_touchPointSlots.find(pointId);
        if (it != m_touchPointSlots.end()) {
            slot = it->second;
        } else {
            for (int i = 0; i < static_cast<int>(m_touchSlotIds.size()); ++i) {
                if (m_touchSlotIds[i] < 0) { slot = i; break; }
            }
            if (slot < 0) return;
            m_touchPointSlots[pointId] = slot;
            m_touchSlotIds[slot] = pointId;
        }
        m_consoleInput->sendMultiTouch({{slot, pointId, x, y}});
    } else {
        auto it = m_touchPointSlots.find(pointId);
        if (it == m_touchPointSlots.end()) return;
        const int slot = it->second;
        m_touchPointSlots.erase(it);
        m_touchSlotIds[slot] = -1;
        m_consoleInput->sendMultiTouch({{slot, -1, 0, 0}});
    }
}

void InputBridge::onTextInput(const std::string &utf8text) {
    if (!m_forwarding || utf8text.empty()) return;
    // Prefer the emulator gRPC text path (real key events, IME-correct).
    if (m_grpcInput) {
        m_grpcInput->sendText(QString::fromStdString(utf8text));
        return;
    }
    if (m_consoleInput && m_consoleInput->isConnected()) {
        m_consoleInput->sendText(utf8text);
        return;
    }
    // ADB fallback: escape spaces for shell command
    std::string escaped;
    escaped.reserve(utf8text.size() + 2);
    for (unsigned char c : utf8text) {
        if (c == '\'') escaped += "'\\''";
        else           escaped += static_cast<char>(c);
    }
    enqueueAdbCommand("input text '" + escaped + "'");
}

// Gamepad button index → Android keycode mapping
static const std::unordered_map<int, int> s_gamepadBtnMap = {
    {0, 96},   // A → KEYCODE_BUTTON_A
    {1, 97},   // B → KEYCODE_BUTTON_B
    {2, 99},   // X → KEYCODE_BUTTON_X
    {3, 100},  // Y → KEYCODE_BUTTON_Y
    {4, 19},   // DPAD_UP → KEYCODE_DPAD_UP
    {5, 20},   // DPAD_DOWN → KEYCODE_DPAD_DOWN
    {6, 21},   // DPAD_LEFT → KEYCODE_DPAD_LEFT
    {7, 22},   // DPAD_RIGHT → KEYCODE_DPAD_RIGHT
    {8, 102},  // LEFT_SHOULDER → KEYCODE_BUTTON_L1
    {9, 103},  // RIGHT_SHOULDER → KEYCODE_BUTTON_R1
    {10, 106}, // LEFT_THUMB → KEYCODE_BUTTON_THUMBL
    {11, 107}, // RIGHT_THUMB → KEYCODE_BUTTON_THUMBR
    {12, 108}, // START → KEYCODE_BUTTON_START
    {13, 109}, // BACK → KEYCODE_BUTTON_SELECT
};

void InputBridge::onGamepadButton(int deviceId, int button, bool pressed) {
    (void)deviceId;
    if (!m_forwarding) return;
    auto it = s_gamepadBtnMap.find(button);
    if (it == s_gamepadBtnMap.end()) return;

    if (hasConsoleKeyboard()) {
        m_consoleInput->sendKeyEvent(it->second, pressed);
        return;
    }

    if (pressed) {
        enqueueAdbCommand("input keyevent " + std::to_string(it->second));
    }
}

void InputBridge::onGamepadAxis(int deviceId, int axis, float value) {
    (void)deviceId;
    if (!m_forwarding) return;
    // Threshold-based swipe for analog sticks
    constexpr float threshold = 0.5f;
    if (std::abs(value) < threshold) return;

    int dx = 0, dy = 0;
    switch (axis) {
    case 0: dx = (value > 0) ? 100 : -100; break; // left stick X
    case 1: dy = (value > 0) ? -100 : 100; break; // left stick Y (inverted)
    case 2: dx = (value > 0) ? 100 : -100; break; // right stick X
    case 3: dy = (value > 0) ? -100 : 100; break; // right stick Y (inverted)
    default: return;
    }

    if (hasConsoleMouse()) {
        m_consoleInput->sendMouseMove((m_displayWidth / 2) + dx, (m_displayHeight / 2) + dy);
    } else if (hasQmp()) {
        m_qmpInput->sendMouseMove((m_displayWidth / 2) + dx, (m_displayHeight / 2) + dy);
    } else {
        const int centerX = m_displayWidth / 2;
        const int centerY = m_displayHeight / 2;
        enqueueAdbCommand("input swipe " + std::to_string(centerX) + " " +
                          std::to_string(centerY) + " " +
                          std::to_string(centerX + dx) + " " +
                          std::to_string(centerY + dy) + " 100",
                          true);
    }
}

bool InputBridge::sendAndroidKeyCode(int androidKeyCode) {
    if (!m_forwarding) return false;
    if (androidKeyCode <= 0) return false;

    // Prefer console path (low latency) over ADB
    if (hasConsoleKeyboard()) {
        m_consoleInput->sendKeyEvent(androidKeyCode, true);
        m_consoleInput->sendKeyEvent(androidKeyCode, false);
        return true;
    }
    if (m_adbPath.empty()) return false;
    enqueueAdbCommand("input keyevent " + std::to_string(androidKeyCode));
    return true;
}

void InputBridge::injectEvent(const Event &ev) {
    if (m_callback) m_callback(ev);

    // HvSocket: highest-priority path for HCS/HyperV backend
    if (hasHvSocket()) {
        switch (ev.type) {
        case Event::MouseButtonDown:
            m_hvSocketTransport->sendMouseButton(ev.code, true);
            break;
        case Event::MouseButtonUp:
            m_hvSocketTransport->sendMouseButton(ev.code, false);
            break;
        case Event::MouseMove: {
            const QPoint hv = m_mapper.guestToHvSocket(QPoint(ev.x, ev.y));
            m_hvSocketTransport->sendMouseMove(hv.x(), hv.y());
            break;
        }
        case Event::KeyDown: {
            const auto it = s_qemuKeyMap.find(ev.code);
            if (it != s_qemuKeyMap.end())
                m_hvSocketTransport->sendKey(it->second, true);
            break;
        }
        case Event::KeyUp: {
            const auto it = s_qemuKeyMap.find(ev.code);
            if (it != s_qemuKeyMap.end())
                m_hvSocketTransport->sendKey(it->second, false);
            break;
        }
        default:
            break;
        }
        return;
    }

    // Per-input-type routing: Console > QMP > ADB
    // Mouse uses Console if probe passed; keyboard uses Console only if keyboard probe passed.
    switch (ev.type) {
    case Event::MouseButtonDown: {
        // Qt button → Android Console bitmask (LeftButton=1, RightButton=2, MiddleButton=4)
        const int consoleBtns = (ev.code == Qt::LeftButton) ? 1 :
                                (ev.code == Qt::RightButton) ? 2 : 4;
        if (m_grpcInput && ev.code == Qt::LeftButton) {
            m_lastGrpcTouchMove = {};
            m_grpcInput->sendTouch(kPrimaryTouchId, ev.x, ev.y, kTouchPressure);
        } else if (hasConsoleMouse()) {
            m_consoleInput->sendMouseEvent(ev.x, ev.y, consoleBtns);
        } else if (hasQmp()) {
            m_qmpInput->sendMouseButton(qtMouseButtonToQmp(ev.code), true, ev.x, ev.y);
        } else if (!m_adbPath.empty()) {
            enqueueAdbCommand("input tap " + std::to_string(ev.x) + " " + std::to_string(ev.y));
        }
        break;
    }
    case Event::MouseButtonUp: {
        if (m_grpcInput && ev.code == Qt::LeftButton) {
            m_grpcInput->sendTouch(kPrimaryTouchId, ev.x, ev.y, 0);
            m_lastGrpcTouchMove = {};
        } else if (hasConsoleMouse()) {
            m_consoleInput->sendMouseEvent(ev.x, ev.y, 0); // release all buttons
        } else if (hasQmp()) {
            m_qmpInput->sendMouseButton(qtMouseButtonToQmp(ev.code), false, ev.x, ev.y);
        }
        // ADB tap covers both down+up; no separate up needed
        break;
    }
    case Event::MouseMove: {
        // ev.code carries held-button bitmask from onMouseButton; 0=hover, 1=left drag, etc.
        if (m_grpcInput && (ev.code & 1) != 0) {
            const auto now = std::chrono::steady_clock::now();
            if (m_lastGrpcTouchMove.time_since_epoch().count() != 0 &&
                now - m_lastGrpcTouchMove < std::chrono::milliseconds(8)) {
                break;
            }
            m_lastGrpcTouchMove = now;
            m_grpcInput->sendTouch(kPrimaryTouchId, ev.x, ev.y, kTouchPressure);
        } else if (hasConsoleMouse()) {
            m_consoleInput->sendMouseEvent(ev.x, ev.y, ev.code);
        } else if (hasQmp()) {
            m_qmpInput->sendMouseMove(ev.x, ev.y);
        } else if (!m_adbPath.empty()) {
            enqueueAdbCommand("input swipe " +
                              std::to_string(ev.x) + " " + std::to_string(ev.y) + " " +
                              std::to_string(ev.x) + " " + std::to_string(ev.y) + " 0",
                              true);
        }
        break;
    }
    case Event::KeyDown: {
        // Keyboard priority: emulator gRPC sendKey (low latency) → QMP → ADB.
        // gRPC/QMP take a Linux evdev code; ADB needs an Android keycode.
        const int linuxKey = mapQtKeyToQemu(ev.code);
        if (m_grpcInput && linuxKey > 0) {
            m_grpcInput->sendKey(linuxKey, true);
        } else if (hasQmp() && linuxKey >= 0) {
            m_qmpInput->sendKey(linuxKey, true);
        } else {
            const int androidKey = mapQtKeyToAndroid(ev.code);
            if (!m_adbPath.empty() && androidKey >= 0)
                enqueueAdbCommand("input keyevent " + std::to_string(androidKey));
        }
        break;
    }
    case Event::KeyUp: {
        const int linuxKey = mapQtKeyToQemu(ev.code);
        if (m_grpcInput && linuxKey > 0) {
            m_grpcInput->sendKey(linuxKey, false);
        } else if (hasQmp() && linuxKey >= 0) {
            m_qmpInput->sendKey(linuxKey, false);
        }
        // ADB has no separate key-up
        break;
    }
    case Event::Wheel: {
        int dx = ev.relX / 30;
        int dy = ev.relY / 30;
        if (dx == 0 && dy == 0) dy = ev.relY > 0 ? -1 : 1;
        const int centerX = (ev.x >= 0) ? ev.x : (m_displayWidth / 2);
        const int centerY = (ev.y >= 0) ? ev.y : (m_displayHeight / 2);
        const int maxX = (m_displayWidth > 0) ? (m_displayWidth - 1) : 0;
        const int maxY = (m_displayHeight > 0) ? (m_displayHeight - 1) : 0;
        const int endX = std::clamp(centerX + dx * 28, 0, maxX);
        const int endY = std::clamp(centerY + dy * 28, 0, maxY);
        if (m_grpcInput) {
            const auto now = std::chrono::steady_clock::now();
            if (m_lastGrpcWheel.time_since_epoch().count() != 0 &&
                now - m_lastGrpcWheel < std::chrono::milliseconds(16)) {
                break;
            }
            m_lastGrpcWheel = now;
            m_grpcInput->sendTouchSwipe(kWheelTouchId, centerX, centerY, endX, endY, 0);
        } else if (!m_adbPath.empty()) {
            enqueueAdbCommand("input swipe " +
                              std::to_string(centerX) + " " + std::to_string(centerY) + " " +
                              std::to_string(endX) + " " +
                              std::to_string(endY) + " 100",
                              true);
        }
        break;
    }
    default:
        break;
    }
}

void InputBridge::setForwardingEnabled(bool enabled) {
    m_forwarding = enabled;
}

} // namespace chimera::input
