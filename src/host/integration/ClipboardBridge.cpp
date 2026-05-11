#include "ClipboardBridge.h"
#include <windows.h>
#include <string>

namespace chimera::integration {

ClipboardBridge &ClipboardBridge::instance() {
    static ClipboardBridge inst;
    return inst;
}

void ClipboardBridge::initialize() {}
void ClipboardBridge::shutdown() {}

void ClipboardBridge::syncHostToGuest() {
    std::string text = getHostText();
    // TODO: send to guest via virtio / gRPC
}

void ClipboardBridge::syncGuestToHost() {
    // TODO: read from guest and setHostText
}

void ClipboardBridge::setHostText(const std::string &text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hMem) {
        char *p = static_cast<char *>(GlobalLock(hMem));
        memcpy(p, text.c_str(), text.size() + 1);
        GlobalUnlock(hMem);
        SetClipboardData(CF_TEXT, hMem);
    }
    CloseClipboard();
}

std::string ClipboardBridge::getHostText() const {
    if (!OpenClipboard(nullptr)) return {};
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) { CloseClipboard(); return {}; }
    char *p = static_cast<char *>(GlobalLock(hData));
    std::string text(p ? p : "");
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

void ClipboardBridge::onGuestClipboardChanged(const std::string &text) {
    setHostText(text);
}

} // namespace chimera::integration
