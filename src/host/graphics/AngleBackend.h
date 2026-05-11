#pragma once

#include <string>
#include <functional>

#ifdef CHIMERA_HAS_ANGLE
#include <EGL/egl.h>
#else
// Minimal EGL type stubs when ANGLE headers are not available
typedef void *EGLDisplay;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef void *EGLConfig;
#define EGL_NO_DISPLAY nullptr
#define EGL_NO_CONTEXT nullptr
#define EGL_NO_SURFACE nullptr
#define EGL_TRUE 1
#define EGL_FALSE 0
#endif

namespace chimera::graphics {

/**
 * @brief ANGLE backend: initializes OpenGL ES context via ANGLE on D3D11/Vulkan.
 *
 * Uses real ANGLE EGL types when headers are available, otherwise stubbed.
 */
class AngleBackend {
public:
    struct Config {
        std::string backend = "d3d11";  // "d3d11", "d3d9", "vulkan", "gl"
        int majorVersion = 3;           // ES 3.0
        int minorVersion = 0;
    };

    using SwapCallback = std::function<void()>;

    static AngleBackend &instance();

    bool initialize(const Config &config);
    void shutdown();

    // Create / destroy surface for a given native window handle
    bool createSurface(void *nativeWindow);
    void destroySurface();

    // Make current and swap
    bool makeCurrent();
    bool swapBuffers();

    // Get_proc_address for guest GLES loading
    void *getProcAddress(const char *name);

    bool isInitialized() const { return m_initialized; }

private:
    AngleBackend() = default;
    bool m_initialized = false;
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    Config m_config;
};

} // namespace chimera::graphics
