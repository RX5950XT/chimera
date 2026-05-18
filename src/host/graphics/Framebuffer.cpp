#include "Framebuffer.h"
#include <cstring>

namespace chimera::graphics {

Framebuffer::Framebuffer(uint32_t width, uint32_t height) {
    resize(width, height);
}

void Framebuffer::resize(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_front.width  = m_back.width  = width;
    m_front.height = m_back.height = height;
    const size_t size = static_cast<size_t>(width) * height * 4;
    m_front.pixels.resize(size);
    m_back.pixels.resize(size);
}

void Framebuffer::writeBackBuffer(const std::vector<uint8_t> &pixels,
                                   uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (width != m_back.width || height != m_back.height) {
        m_front.width  = m_back.width  = width;
        m_front.height = m_back.height = height;
        const size_t size = static_cast<size_t>(width) * height * 4;
        m_front.pixels.resize(size);
        m_back.pixels.resize(size);
    }
    if (pixels.size() == m_back.pixels.size())
        std::memcpy(m_back.pixels.data(), pixels.data(), pixels.size());
}

// Returns a snapshot copy so callers don't hold a reference across a swap()
Framebuffer::Buffer Framebuffer::readFrontBuffer() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_front;
}

void Framebuffer::swap() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::swap(m_front, m_back);
}

uint32_t Framebuffer::width() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_front.width;
}

uint32_t Framebuffer::height() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_front.height;
}

} // namespace chimera::graphics
