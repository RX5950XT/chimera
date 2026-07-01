#include "MacroEngine.h"
#include "InputBridge.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>
#include <chrono>

namespace chimera::input {

using json = nlohmann::json;

static bool isSafeMacroName(const std::string &name) {
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
    });
}

MacroEngine &MacroEngine::instance() {
    static MacroEngine inst;
    return inst;
}

MacroEngine::~MacroEngine() {
    stopPlayback();
}

void MacroEngine::startRecording(const std::string &name) {
    if (!isSafeMacroName(name)) return;

    m_recording = true;
    m_currentMacroName = name;
    m_events.clear();
    m_recordingStart = std::chrono::steady_clock::now();
}

void MacroEngine::stopRecording() {
    m_recording = false;
    saveMacro(m_currentMacroName);
}

bool MacroEngine::isRecording() const {
    return m_recording;
}

void MacroEngine::recordEvent(const MacroEvent &event) {
    if (!m_recording) return;
    m_events.push_back(event);
}

void MacroEngine::startPlayback(const std::string &name, int loopCount) {
    if (m_playbackThread.joinable()) {
        stopPlayback();
    }
    if (!loadMacro(name)) return;
    if (m_events.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_playing = true;
        m_stopPlayback = false;
    }
    m_playbackThread = std::thread([this, loopCount]() {
        playbackLoop(loopCount);
    });
}

void MacroEngine::playbackLoop(int loopCount) {
    auto &bridge = InputBridge::instance();
    for (int loop = 0; loop < loopCount; ++loop) {
        auto start = std::chrono::steady_clock::now();
        for (const auto &ev : m_events) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_stopPlayback) { m_playing = false; return; }
            }
            // Wait until event timestamp
            auto target = start + ev.timestamp;
            std::this_thread::sleep_until(target);

            // Inject event through InputBridge
            switch (ev.type) {
            case MacroEvent::Tap:
                bridge.onMouseButton(true, 1, ev.x, ev.y); // 1 = left button
                bridge.onMouseButton(false, 1, ev.x, ev.y);
                break;
            case MacroEvent::Swipe:
                bridge.onMouseMove(ev.x, ev.y, 0, 0);
                break;
            case MacroEvent::KeyPress:
                bridge.onKeyEvent(true, 0, ev.keyCode);
                break;
            case MacroEvent::KeyRelease:
                bridge.onKeyEvent(false, 0, ev.keyCode);
                break;
            case MacroEvent::Delay:
                // Already handled by timestamp
                break;
            }
        }
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playing = false;
}

void MacroEngine::stopPlayback() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopPlayback = true;
        m_playing = false;
    }
    m_cv.notify_all();
    if (m_playbackThread.joinable()) m_playbackThread.join();
}

bool MacroEngine::isPlaying() const {
    return m_playing;
}

std::vector<std::string> MacroEngine::listMacros() const {
    std::vector<std::string> names;
    auto dir = std::filesystem::path("configs/macros");
    if (!std::filesystem::exists(dir)) return names;
    for (auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".json") {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

bool MacroEngine::loadMacro(const std::string &name) {
    if (!isSafeMacroName(name)) return false;

    auto path = std::filesystem::path("configs/macros") / (name + ".json");
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        m_events.clear();
        for (auto &e : j["events"]) {
            MacroEvent ev;
            ev.type = static_cast<MacroEvent::Type>(e["type"].get<int>());
            ev.timestamp = std::chrono::milliseconds(e["timestamp"].get<int>());
            ev.x = e.value("x", 0);
            ev.y = e.value("y", 0);
            ev.keyCode = e.value("keyCode", 0);
            ev.key = e.value("key", "");
            m_events.push_back(ev);
        }
        m_currentMacroName = name;
        return true;
    } catch (...) {
        return false;
    }
}

bool MacroEngine::saveMacro(const std::string &name) const {
    if (!isSafeMacroName(name)) return false;

    auto dir = std::filesystem::path("configs/macros");
    std::filesystem::create_directories(dir);
    auto path = dir / (name + ".json");
    std::ofstream f(path);
    if (!f.is_open()) return false;
    json j;
    j["name"] = name;
    json events = json::array();
    for (auto &e : m_events) {
        json ej;
        ej["type"] = static_cast<int>(e.type);
        ej["timestamp"] = e.timestamp.count();
        ej["x"] = e.x;
        ej["y"] = e.y;
        ej["keyCode"] = e.keyCode;
        ej["key"] = e.key;
        events.push_back(ej);
    }
    j["events"] = events;
    f << j.dump(2);
    return true;
}

bool MacroEngine::deleteMacro(const std::string &name) {
    if (!isSafeMacroName(name)) return false;

    auto path = std::filesystem::path("configs/macros") / (name + ".json");
    return std::filesystem::remove(path);
}

} // namespace chimera::input
