#pragma once

#include <string>
#include <vector>

namespace chimera::integration {

/**
 * @brief Bridges Android guest notifications to Windows notification center.
 */
class WindowsNotifier {
public:
    static WindowsNotifier &instance();

    void initialize();
    void shutdown();

    void showNotification(const std::string &title, const std::string &body,
                          const std::string &iconPath = {});

    // Called when guest sends a notification via virtio-sock / gRPC
    void onGuestNotification(const std::string &packageName,
                             const std::string &title,
                             const std::string &text);
};

} // namespace chimera::integration
