#include "InputBridge.h"
#include "QmpInput.h"
#include <windows.h>
#include <QProcess>
#include <QDebug>
#include <unordered_map>
#include <cmath>

namespace chimera::input {

// Qt keycode → Android keycode mapping (common keys)
static const std::unordered_map<int, int> s_keyMap = {
    {0x01000000, 111}, // Qt::Key_Escape → KEYCODE_ESCAPE
    {0x01000001, 1},   // Qt::Key_Tab → KEYCODE_TAB
    {0x01000003, 124}, // Qt::Key_Backspace → KEYCODE_DEL
    {0x01000004, 112}, // Qt::Key_Return → KEYCODE_ENTER
    {0x01000005, 66},  // Qt::Key_Enter → KEYCODE_ENTER
    {0x01000010, 59},  // Qt::Key_Shift → KEYCODE_SHIFT_LEFT
    {0x01000011, 60},  // Qt::Key_Control → KEYCODE_CTRL_LEFT
    {0x01000012, 113}, // Qt::Key_Alt → KEYCODE_ALT_LEFT
    {0x01000013, 114}, // Qt::Key_CapsLock → KEYCODE_CAPS_LOCK
    {0x01000014, 115}, // Qt::Key_NumLock → KEYCODE_NUM_LOCK
    {0x01000015, 121}, // Qt::Key_ScrollLock → KEYCODE_SCROLL_LOCK
    {0x01000016, 122}, // Qt::Key_Pause → KEYCODE_BREAK
    {0x01000020, 67},  // Qt::Key_Space → KEYCODE_SPACE
    {0x01000030, 7},   // Qt::Key_0 → KEYCODE_0
    {0x01000031, 8},   // Qt::Key_1 → KEYCODE_1
    {0x01000032, 9},   // Qt::Key_2 → KEYCODE_2
    {0x01000033, 10},  // Qt::Key_3 → KEYCODE_3
    {0x01000034, 11},  // Qt::Key_4 → KEYCODE_4
    {0x01000035, 12},  // Qt::Key_5 → KEYCODE_5
    {0x01000036, 13},  // Qt::Key_6 → KEYCODE_6
    {0x01000037, 14},  // Qt::Key_7 → KEYCODE_7
    {0x01000038, 15},  // Qt::Key_8 → KEYCODE_8
    {0x01000039, 16},  // Qt::Key_9 → KEYCODE_9
    {0x01000041, 29},  // Qt::Key_A → KEYCODE_A
    {0x01000042, 30},  // Qt::Key_B → KEYCODE_B
    {0x01000043, 31},  // Qt::Key_C → KEYCODE_C
    {0x01000044, 32},  // Qt::Key_D → KEYCODE_D
    {0x01000045, 33},  // Qt::Key_E → KEYCODE_E
    {0x01000046, 34},  // Qt::Key_F → KEYCODE_F
    {0x01000047, 35},  // Qt::Key_G → KEYCODE_G
    {0x01000048, 36},  // Qt::Key_H → KEYCODE_H
    {0x01000049, 37},  // Qt::Key_I → KEYCODE_I
    {0x0100004A, 38},  // Qt::Key_J → KEYCODE_J
    {0x0100004B, 39},  // Qt::Key_K → KEYCODE_K
    {0x0100004C, 40},  // Qt::Key_L → KEYCODE_L
    {0x0100004D, 41},  // Qt::Key_M → KEYCODE_M
    {0x0100004E, 42},  // Qt::Key_N → KEYCODE_N
    {0x0100004F, 43},  // Qt::Key_O → KEYCODE_O
    {0x01000050, 44},  // Qt::Key_P → KEYCODE_P
    {0x01000051, 45},  // Qt::Key_Q → KEYCODE_Q
    {0x01000052, 46},  // Qt::Key_R → KEYCODE_R
    {0x01000053, 47},  // Qt::Key_S → KEYCODE_S
    {0x01000054, 48},  // Qt::Key_T → KEYCODE_T
    {0x01000055, 49},  // Qt::Key_U → KEYCODE_U
    {0x01000056, 50},  // Qt::Key_V → KEYCODE_V
    {0x01000057, 51},  // Qt::Key_W → KEYCODE_W
    {0x01000058, 52},  // Qt::Key_X → KEYCODE_X
    {0x01000059, 53},  // Qt::Key_Y → KEYCODE_Y
    {0x0100005A, 54},  // Qt::Key_Z → KEYCODE_Z
    {0x01000060, 131}, // Qt::Key_F1 → KEYCODE_F1
    {0x01000061, 132}, // Qt::Key_F2 → KEYCODE_F2
    {0x01000062, 133}, // Qt::Key_F3 → KEYCODE_F3
    {0x01000063, 134}, // Qt::Key_F4 → KEYCODE_F4
    {0x01000064, 135}, // Qt::Key_F5 → KEYCODE_F5
    {0x01000065, 136}, // Qt::Key_F6 → KEYCODE_F6
    {0x01000066, 137}, // Qt::Key_F7 → KEYCODE_F7
    {0x01000067, 138}, // Qt::Key_F8 → KEYCODE_F8
    {0x01000068, 139}, // Qt::Key_F9 → KEYCODE_F9
    {0x01000069, 140}, // Qt::Key_F10 → KEYCODE_F10
    {0x0100006A, 141}, // Qt::Key_F11 → KEYCODE_F11
    {0x0100006B, 142}, // Qt::Key_F12 → KEYCODE_F12
    {0x01000090, 92},  // Qt::Key_Left → KEYCODE_DPAD_LEFT
    {0x01000091, 93},  // Qt::Key_Up → KEYCODE_DPAD_UP
    {0x01000092, 94},  // Qt::Key_Right → KEYCODE_DPAD_RIGHT
    {0x01000093, 95},  // Qt::Key_Down → KEYCODE_DPAD_DOWN
    {0x0100010E, 126}, // Qt::Key_Meta → KEYCODE_META_LEFT
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

InputBridge &InputBridge::instance() {
    static InputBridge inst;
    return inst;
}

InputBridge::InputBridge() : m_worker([this]() { workerLoop(); }) {}

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

void InputBridge::setAdbConfig(const std::filesystem::path &adbPath, int adbPort) {
    m_adbPath = adbPath;
    m_adbPort = adbPort;
}

void InputBridge::setQmpInput(QmpInput *qmp) {
    m_qmpInput = qmp;
}

bool InputBridge::hasQmp() const {
    return m_qmpInput != nullptr && m_qmpInput->isConnected();
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
        args << "-P" << QString::number(m_adbPort);
        args << "shell" << QString::fromStdString(cmd);
        QProcess proc;
        proc.setProgram(QString::fromStdString(m_adbPath.string()));
        proc.setArguments(args);
        proc.start();
        proc.waitForFinished(500);
    }
}

void InputBridge::enqueueAdbCommand(const std::string &cmd) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) return;
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

void InputBridge::onKeyEvent(bool press, int nativeScanCode, int nativeVirtualKey) {
    (void)nativeScanCode;
    if (!m_forwarding) return;
    Event ev;
    ev.type = press ? Event::KeyDown : Event::KeyUp;
    ev.code = nativeVirtualKey;
    injectEvent(ev);
}

void InputBridge::onMouseMove(int x, int y, int dx, int dy) {
    (void)dx; (void)dy;
    if (!m_forwarding) return;
    Event ev;
    ev.type = Event::MouseMove;
    ev.x = x; ev.y = y;
    injectEvent(ev);
}

void InputBridge::onMouseButton(bool press, int button, int x, int y) {
    if (!m_forwarding) return;
    Event ev;
    ev.type = press ? Event::MouseButtonDown : Event::MouseButtonUp;
    ev.code = button;
    ev.x = x; ev.y = y;
    injectEvent(ev);
}

void InputBridge::onWheel(int deltaX, int deltaY) {
    if (!m_forwarding) return;
    Event ev;
    ev.type = Event::Wheel;
    ev.relX = deltaX; ev.relY = deltaY;
    injectEvent(ev);
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

    if (hasQmp()) {
        // Map gamepad buttons to QEMU keycodes (use KEY_LEFTCTRL etc. as placeholders)
        // For now, fall through to ADB for gamepad
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

    if (hasQmp()) {
        m_qmpInput->sendMouseMove(960 + dx, 540 + dy);
    } else {
        enqueueAdbCommand("input swipe 960 540 " +
                          std::to_string(960 + dx) + " " +
                          std::to_string(540 + dy) + " 100");
    }
}

void InputBridge::injectEvent(const Event &ev) {
    if (m_callback) m_callback(ev);

    // Prefer QMP for low-latency input injection
    if (hasQmp()) {
        switch (ev.type) {
        case Event::MouseButtonDown: {
            m_qmpInput->sendMouseButton(0, true, ev.x, ev.y);
            break;
        }
        case Event::MouseButtonUp: {
            m_qmpInput->sendMouseButton(0, false, ev.x, ev.y);
            break;
        }
        case Event::MouseMove: {
            m_qmpInput->sendMouseMove(ev.x, ev.y);
            break;
        }
        case Event::KeyDown: {
            int qemuKey = mapQtKeyToQemu(ev.code);
            if (qemuKey >= 0) {
                m_qmpInput->sendKey(qemuKey, true);
            }
            break;
        }
        case Event::KeyUp: {
            int qemuKey = mapQtKeyToQemu(ev.code);
            if (qemuKey >= 0) {
                m_qmpInput->sendKey(qemuKey, false);
            }
            break;
        }
        default:
            break;
        }
        return;
    }

    // Fallback: ADB
    if (m_adbPath.empty()) return;

    switch (ev.type) {
    case Event::MouseButtonDown: {
        enqueueAdbCommand("input tap " + std::to_string(ev.x) + " " + std::to_string(ev.y));
        break;
    }
    case Event::MouseButtonUp:
        break;
    case Event::MouseMove: {
        enqueueAdbCommand("input swipe " + std::to_string(ev.x) + " " + std::to_string(ev.y) +
                          " " + std::to_string(ev.x) + " " + std::to_string(ev.y) + " 0");
        break;
    }
    case Event::KeyDown: {
        int androidKey = mapQtKeyToAndroid(ev.code);
        if (androidKey >= 0) {
            enqueueAdbCommand("input keyevent " + std::to_string(androidKey));
        }
        break;
    }
    case Event::KeyUp:
        break;
    case Event::Wheel: {
        int dx = ev.relX / 40;
        int dy = ev.relY / 40;
        if (dx == 0 && dy == 0) dy = ev.relY > 0 ? -1 : 1;
        enqueueAdbCommand("input swipe 960 540 " + std::to_string(960 + dx * 50) + " " +
                          std::to_string(540 + dy * 50) + " 100");
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
