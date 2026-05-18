#include "ClipboardBridge.h"
#include <windows.h>
#include <string>

namespace chimera::integration {

namespace {

// UTF-8 → UTF-16
static std::wstring toWide(const std::string &utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), len);
    return wide;
}

// UTF-16 → UTF-8
static std::string fromWide(const wchar_t *wide) {
    if (!wide || *wide == L'\0') return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string utf8(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), len, nullptr, nullptr);
    return utf8;
}

} // namespace

ClipboardBridge &ClipboardBridge::instance() {
    static ClipboardBridge inst;
    return inst;
}

void ClipboardBridge::initialize() {}
void ClipboardBridge::shutdown() {}

void ClipboardBridge::setGuestSink(GuestSink sink) {
    m_guestSink = std::move(sink);
}

void ClipboardBridge::setHostText(const std::string &utf8text) {
    const std::wstring wide = toWide(utf8text);
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const size_t byteSize = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
    if (hMem) {
        wchar_t *p = static_cast<wchar_t *>(GlobalLock(hMem));
        if (p) {
            memcpy(p, wide.c_str(), byteSize);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

std::string ClipboardBridge::getHostText() const {
    if (!OpenClipboard(nullptr)) return {};

    // Prefer UTF-16 to preserve CJK / emoji
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t *p = static_cast<const wchar_t *>(GlobalLock(hData));
        std::string result = fromWide(p);
        GlobalUnlock(hData);
        CloseClipboard();
        return result;
    }

    // Fallback: legacy CF_TEXT (ANSI, may lose non-ASCII)
    hData = GetClipboardData(CF_TEXT);
    if (hData) {
        const char *p = static_cast<const char *>(GlobalLock(hData));
        std::string result(p ? p : "");
        GlobalUnlock(hData);
        CloseClipboard();
        return result;
    }

    CloseClipboard();
    return {};
}

void ClipboardBridge::syncHostToGuest() {
    if (!m_guestSink) return;
    const std::string text = getHostText();
    if (!text.empty())
        m_guestSink(text);
}

void ClipboardBridge::syncGuestToHost(const std::string &utf8text) {
    setHostText(utf8text);
}

void ClipboardBridge::onGuestClipboardChanged(const std::string &utf8text) {
    syncGuestToHost(utf8text);
}

} // namespace chimera::integration
