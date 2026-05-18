#include "AngleBackend.h"
#include "EglLoader.h"
#include <stdexcept>
#include <QDebug>

namespace chimera::graphics {

AngleBackend &AngleBackend::instance() {
    static AngleBackend inst;
    return inst;
}

bool AngleBackend::initialize(const Config &config) {
    m_config = config;

#ifdef CHIMERA_HAS_ANGLE
    if (!egl().load()) {
        qWarning() << "ANGLE: Failed to load libEGL.dll";
        return false;
    }

    m_display = egl().eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY) {
        qWarning() << "ANGLE: eglGetDisplay failed";
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!egl().eglInitialize(m_display, &major, &minor)) {
        qWarning() << "ANGLE: eglInitialize failed";
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig cfg;
    EGLint numConfigs = 0;
    if (!egl().eglChooseConfig(m_display, attribs, &cfg, 1, &numConfigs) || numConfigs < 1) {
        qWarning() << "ANGLE: eglChooseConfig failed";
        egl().eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, m_config.majorVersion,
        EGL_NONE
    };

    m_context = egl().eglCreateContext(m_display, cfg, EGL_NO_CONTEXT, contextAttribs);
    if (m_context == EGL_NO_CONTEXT) {
        qWarning() << "ANGLE: eglCreateContext failed";
        egl().eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
        return false;
    }

    m_initialized = true;
    qDebug() << "ANGLE initialized: EGL" << major << "." << minor;
    return true;
#else
    (void)config;
    qWarning() << "ANGLE: headers not available, stubbed";
    m_initialized = false;
    return false;
#endif
}

void AngleBackend::shutdown() {
#ifdef CHIMERA_HAS_ANGLE
    if (!egl().loaded) return;
    if (m_display != EGL_NO_DISPLAY) {
        egl().eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface != EGL_NO_SURFACE) {
            egl().eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
        }
        if (m_context != EGL_NO_CONTEXT) {
            egl().eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
        }
        egl().eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
    }
#endif
    m_initialized = false;
}

bool AngleBackend::createSurface(void *nativeWindow) {
#ifdef CHIMERA_HAS_ANGLE
    if (!m_initialized || m_display == EGL_NO_DISPLAY || !egl().loaded) return false;
    if (m_surface != EGL_NO_SURFACE) destroySurface();

    m_surface = egl().eglCreateWindowSurface(m_display, nullptr, reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
    return m_surface != EGL_NO_SURFACE;
#else
    (void)nativeWindow;
    return false;
#endif
}

void AngleBackend::destroySurface() {
#ifdef CHIMERA_HAS_ANGLE
    if (egl().loaded && m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE) {
        egl().eglDestroySurface(m_display, m_surface);
    }
#endif
    m_surface = EGL_NO_SURFACE;
}

bool AngleBackend::makeCurrent() {
#ifdef CHIMERA_HAS_ANGLE
    if (!m_initialized || !egl().loaded) return false;
    return egl().eglMakeCurrent(m_display, m_surface, m_surface, m_context) == EGL_TRUE;
#else
    return false;
#endif
}

bool AngleBackend::swapBuffers() {
#ifdef CHIMERA_HAS_ANGLE
    if (!m_initialized || !egl().loaded) return false;
    return egl().eglSwapBuffers(m_display, m_surface) == EGL_TRUE;
#else
    return false;
#endif
}

void *AngleBackend::getProcAddress(const char *name) {
#ifdef CHIMERA_HAS_ANGLE
    if (!m_initialized || !egl().loaded) return nullptr;
    return reinterpret_cast<void*>(egl().eglGetProcAddress(name));
#else
    (void)name;
    return nullptr;
#endif
}

} // namespace chimera::graphics
