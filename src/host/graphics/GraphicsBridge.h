#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

namespace chimera::graphics {

/**
 * @brief Bridges Android guest graphics (OpenGL ES / Vulkan) to Host GPU.
 *
 * Receives rendered frames from QEMU / VirtIO-GPU and forwards them to the Host UI.
 */
class GraphicsBridge {
public:
    struct Frame {
        uint32_t width, height;
        uint32_t format;  // RGBA, BGRA, etc.
        std::vector<uint8_t> data;
    };

    using FrameCallback = std::function<void(const Frame &)>;

    static GraphicsBridge &instance();

    bool initialize();
    void shutdown();

    // Called when a new frame is ready from the guest
    void onGuestFrame(const Frame &frame);

    // Host UI subscribes to receive frames
    void setFrameCallback(FrameCallback cb);

    // Resize guest framebuffer
    void setResolution(uint32_t width, uint32_t height);

private:
    GraphicsBridge() = default;
    FrameCallback m_callback;
    uint32_t m_width = 1920, m_height = 1080;
};

} // namespace chimera::graphics
