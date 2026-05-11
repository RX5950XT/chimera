#pragma once

#include <string>

namespace chimera::integration {

/**
 * @brief Bidirectional clipboard sync between Host Windows and Guest Android.
 */
class ClipboardBridge {
public:
    static ClipboardBridge &instance();

    void initialize();
    void shutdown();

    // Host -> Guest
    void syncHostToGuest();

    // Guest -> Host
    void syncGuestToHost();

    // Set/get host clipboard text
    void setHostText(const std::string &text);
    std::string getHostText() const;

    // Called when guest clipboard changes
    void onGuestClipboardChanged(const std::string &text);
};

} // namespace chimera::integration
