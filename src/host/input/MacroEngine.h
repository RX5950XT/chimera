#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace chimera::input {

/**
 * @brief Records and plays back input sequences (macros).
 *
 * Inspired by BlueStacks macro system. Stores tap/swipe/key events with timestamps.
 */
struct MacroEvent {
    enum Type { Tap, Swipe, KeyPress, KeyRelease, Delay };
    Type type;
    std::chrono::milliseconds timestamp;  // Relative to macro start
    int x, y;                             // For tap/swipe
    int keyCode;                          // For key events
    std::string key;                      // Human-readable key name
};

class MacroEngine {
public:
    static MacroEngine &instance();
    ~MacroEngine();

    // Recording
    void startRecording(const std::string &name);
    void stopRecording();
    bool isRecording() const;
    void recordEvent(const MacroEvent &event);

    // Playback
    void startPlayback(const std::string &name, int loopCount = 1);
    void stopPlayback();
    bool isPlaying() const;

    // Management
    std::vector<std::string> listMacros() const;
    bool loadMacro(const std::string &name);
    bool saveMacro(const std::string &name) const;
    bool deleteMacro(const std::string &name);

private:
    MacroEngine() = default;
    void playbackLoop(int loopCount);

    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_playing{false};
    std::string m_currentMacroName;
    std::vector<MacroEvent> m_events;

    std::thread m_playbackThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopPlayback = false;
};

} // namespace chimera::input
