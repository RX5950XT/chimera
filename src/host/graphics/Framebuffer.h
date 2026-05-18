#pragma once

#include <cstdint>
#include <vector>
#include <mutex>

namespace chimera::graphics {

/**
 * @brief Double-buffered framebuffer for host-guest frame exchange.
 */
class Framebuffer {
public:
    struct Buffer {
        uint32_t width, height;
        std::vector<uint8_t> pixels;  // RGBA8
    };

    Framebuffer(uint32_t width, uint32_t height);

    void resize(uint32_t width, uint32_t height);

    // Producer (guest renderer) writes to back buffer
    void writeBackBuffer(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height);

    // Consumer (host UI) — returns a snapshot copy under lock (no dangling reference)
    Buffer readFrontBuffer() const;

    // Swap buffers (called at frame boundary)
    void swap();

    uint32_t width() const;
    uint32_t height() const;

private:
    Buffer m_front;
    Buffer m_back;
    mutable std::mutex m_mutex;
};

} // namespace chimera::graphics
