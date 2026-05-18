#pragma once

#include <string>
#include <functional>

namespace chimera::integration {

/**
 * @brief Bidirectional clipboard sync between Host Windows and Guest Android.
 *
 * Uses CF_UNICODETEXT to preserve CJK characters and emoji.
 * Wire setGuestSink() to AndroidConsoleInput::sendClipboardSet so that
 * syncHostToGuest() delivers content via the emulator console channel.
 */
class ClipboardBridge {
public:
    // Sink receives UTF-8 text to send to the guest
    using GuestSink = std::function<void(const std::string &utf8text)>;

    static ClipboardBridge &instance();

    void initialize();
    void shutdown();

    // Wire the channel used to push text to the guest
    void setGuestSink(GuestSink sink);

    // Host → Guest: read Windows clipboard (CF_UNICODETEXT), push via sink
    void syncHostToGuest();

    // Guest → Host: receive UTF-8 text from guest, write to Windows clipboard
    void syncGuestToHost(const std::string &utf8text);

    // Write UTF-8 text to the Windows clipboard as CF_UNICODETEXT
    void setHostText(const std::string &utf8text);

    // Read Windows clipboard as UTF-8 (prefers CF_UNICODETEXT over CF_TEXT)
    std::string getHostText() const;

    // Called when the guest clipboard changes (e.g. from a guest notification)
    void onGuestClipboardChanged(const std::string &utf8text);

private:
    ClipboardBridge() = default;
    GuestSink m_guestSink;
};

} // namespace chimera::integration
