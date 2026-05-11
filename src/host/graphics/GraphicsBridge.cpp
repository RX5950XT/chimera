#include "GraphicsBridge.h"

namespace chimera::graphics {

GraphicsBridge &GraphicsBridge::instance() {
    static GraphicsBridge inst;
    return inst;
}

bool GraphicsBridge::initialize() {
    return true;
}

void GraphicsBridge::shutdown() {
}

void GraphicsBridge::onGuestFrame(const Frame &frame) {
    if (m_callback) {
        m_callback(frame);
    }
}

void GraphicsBridge::setFrameCallback(FrameCallback cb) {
    m_callback = cb;
}

void GraphicsBridge::setResolution(uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;
}

} // namespace chimera::graphics
