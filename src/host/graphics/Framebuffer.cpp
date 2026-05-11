#include "Framebuffer.h"
#include <cstring>

namespace chimera::graphics {

Framebuffer::Framebuffer(uint32_t width, uint32_t height) {
    resize(width, height);
}

void Framebuffer::resize(uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_front.width = m_back.width = width;
    m_front.height = m_back.height = height;
    size_t size = width * height * 4;
    m_front.pixels.resize(size);
    m_back.pixels.resize(size);
}

void Framebuffer::writeBackBuffer(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (width != m_back.width || height != m_back.height) {
        resize(width, height);
    }
    if (pixels.size() == m_back.pixels.size()) {
        std::memcpy(m_back.pixels.data(), pixels.data(), pixels.size());
    }
}

const Framebuffer::Buffer &Framebuffer::readFrontBuffer() const {
    return m_front;
}

void Framebuffer::swap() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::swap(m_front, m_back);
}

} // namespace chimera::graphics
