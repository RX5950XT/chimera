#include "WindowsNotifier.h"
#include <windows.h>
#include <shellapi.h>

namespace chimera::integration {

WindowsNotifier &WindowsNotifier::instance() {
    static WindowsNotifier inst;
    return inst;
}

void WindowsNotifier::initialize() {}
void WindowsNotifier::shutdown() {}

void WindowsNotifier::showNotification(const std::string &title, const std::string &body, const std::string &iconPath) {
    // Use Windows 10/11 toast notifications via WinRT or fallback to Shell_NotifyIcon
    // Simplified fallback using MessageBox for now
    // TODO: implement proper toast notification
    MessageBoxA(nullptr, body.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

void WindowsNotifier::onGuestNotification(const std::string &packageName, const std::string &title, const std::string &text) {
    showNotification(title, text);
}

} // namespace chimera::integration
